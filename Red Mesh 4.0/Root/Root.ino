#include "painlessMesh.h"
#include <PZEM004Tv30.h>
#include <DHT.h>
#include <RTClib.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// Configuracion red Mesh
#define   MESH_PREFIX     "whateverYouLike"
#define   MESH_PASSWORD   "somethingSneaky"
#define   MESH_PORT       5555
painlessMesh  mesh;

// === MÉTRICAS DE RENDIMIENTO ===
unsigned long lastRateTime = 0;
int packetsReceived = 0;
int bytesReceived = 0;

int reconexiones = 0;

unsigned long lastMsgTime[20];  // hasta 20 nodos (ajustable)


//I2C ESP
#define I2C_SLAVE_ADDRESS 0x08

// CONFIGURACION DE PANTALLA OLED
#define i2c_Address 0x3c
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Configuración del PZEM (Modulo de voltaje, corriente, potencia y energia)
PZEM004Tv30 pzem(Serial2, 16, 17);
float VOLTAJE, CORRIENTE, POTENCIA, ENERGIA, TEMPERATURA;
int HUMEDAD;
char mensaje[100];

// CONFIGURACION PARA DHT11
#define DPIN 4        // Pin to connect DHT sensor (GPIO number)
#define DTYPE DHT11   // Define DHT 11 or DHT22 sensor type
DHT dht(DPIN, DTYPE);

// CONFIGURACION RTC
RTC_DS3231 rtc;
char daysOfWeek[7][12] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

bool dataSent = false; // Add a flag to track whether data has been sent

// Arreglo para almacenar los últimos cinco datos de energía enviados
float energiaDatos[6] = {0}; 
int indice = 0;

bool condition_met() {
  // Verificar si falta 1 minuto para cada múltiplo de 10 minutos
  int currentMinute = rtc.now().minute();
  if (currentMinute % 10 == 1) { 
    return true;
  } else {
    dataSent = false; // Reset the flag when the condition is no longer met
    return false;
  }
}

// === CALCULAR LATENCIA A PARTIR DEL TIMESTAMP RECIBIDO ===
unsigned long calcularLatencia(String msg) {
  int p = msg.indexOf(',');
  if (p < 0) return 0;

  String ts = msg.substring(0, p);  // "YYYY-MM-DD HH:MM:SS"

  int year = ts.substring(0,4).toInt();
  int month = ts.substring(5,7).toInt();
  int day = ts.substring(8,10).toInt();
  int hour = ts.substring(11,13).toInt();
  int minute = ts.substring(14,16).toInt();
  int second = ts.substring(17,19).toInt();

  DateTime msgTime(year, month, day, hour, minute, second);
  DateTime now = rtc.now();

  return now.unixtime() - msgTime.unixtime();
}

// Función para calcular el valor máximo de los últimos cinco datos de energía
float obtenerValorMaximo() {
  float maximo = energiaDatos[0];
  for (int i = 1; i < 6; i++) {
    if (energiaDatos[i] > maximo) {
      maximo = energiaDatos[i];
    }
  }
  return maximo;
}

// Estructura para almacenar datos de nodos
struct NodoData {
  uint32_t id;
  String nombre;
  float energia;
  float temperatura;
  int humedad;
  DateTime timestamp;
};


std::vector<NodoData> nodos;
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 600000; // 10 minutos en milisegundos

// Función para separar los datos recibidos y almacenarlos
void separarDatos(String msg, uint32_t from) {
  NodoData nodo;
  nodo.id = from;
  nodo.timestamp = rtc.now();

  // Extraer campos separados por coma
  int p1 = msg.indexOf(',');
  int p2 = msg.indexOf(',', p1 + 1);
  int p3 = msg.indexOf(',', p2 + 1);
  int p4 = msg.indexOf(',', p3 + 1);

  // Convertir campos
  String idStr       = msg.substring(0, p1);      // nodeId enviado
  nodo.nombre        = msg.substring(p1 + 1, p2); // nombre del nodo
  nodo.energia       = msg.substring(p2 + 1, p3).toFloat();
  nodo.temperatura   = msg.substring(p3 + 1, p4).toFloat();
  nodo.humedad       = msg.substring(p4 + 1).toInt();

  // Buscar si ya existe y actualizar
  bool found = false;
  for (auto &n : nodos) {
    if (n.id == from) {
      n = nodo;
      found = true;
      break;
    }
  }
  if (!found) {
    nodos.push_back(nodo);
  }
}

void receivedCallback(uint32_t from, String &msg) {
  Serial.printf("Mensaje recibido de %u: %s\n", from, msg.c_str());

  // === Tasa de recepción de paquetes ===
  packetsReceived++;              // Incrementa el contador de paquetes recibidos
  bytesReceived += msg.length();  // Acumula los bytes recibidos

  // === Nivel de señal recibida (RSSI) ===
  int rssi = WiFi.RSSI();   // Obtiene la potencia de señal en dBm
  Serial.printf("RSSI nodo %u = %d dBm\n", from, rssi);

  // === Latencia de comunicación ===
  unsigned long lat = calcularLatencia(msg);  // Calcula latencia según marca de tiempo en msg
  Serial.printf("Latencia nodo %u = %lu segundos\n", from, lat);

  // === PAQUETE ORIGINAL HACIA I2C ===
  DateTime now = rtc.now();
  String dataBuffer = String(now.year()) + "-" +
                      twoDigits(now.month()) + "-" +
                      twoDigits(now.day()) + " " +
                      twoDigits(now.hour()) + ":" +
                      twoDigits(now.minute()) + ":" +
                      twoDigits(now.second()) + "," +
                      msg;

  Wire.beginTransmission(I2C_SLAVE_ADDRESS);
  Wire.write((const uint8_t*)dataBuffer.c_str(), dataBuffer.length());
  Wire.endTransmission();

  // === 5. ENVÍO DEL PAQUETE DE TEST POR I2C ===
  String testData = 
        String(now.year()) + "-" +
        twoDigits(now.month()) + "-" +
        twoDigits(now.day()) + " " +
        twoDigits(now.hour()) + ":" +
        twoDigits(now.minute()) + ":" +
        twoDigits(now.second()) + "," +
        "TEST," +
        String(packetsReceived) + "," +     // TxRate (paquetes recibidos)
        String(lat) + "," +                 // Latencia
        String(reconexiones) + "," +        // Estabilidad (reconexiones)
        String(rssi);                       // RSSI

  Wire.beginTransmission(I2C_SLAVE_ADDRESS);
  Wire.write((const uint8_t*)testData.c_str(), testData.length());
  Wire.endTransmission();
}

String twoDigits(int number) {
  if (number < 10) return "0" + String(number);
  return String(number);
}

void newConnectionCallback(uint32_t nodeId) {
  Serial.printf("--> startHere: New Connection, nodeId = %u\n", nodeId);
}

void changedConnectionCallback() {
  // === Estabilidad de la red (reconexiones) ===
  reconexiones++; // Incrementa el contador cada vez que se detecta reconexión
  Serial.printf("Conexiones cambiaron. Reconexiones acumuladas: %d\n", reconexiones);
}

void nodeTimeAdjustedCallback(int32_t offset) {
  Serial.printf("Adjusted time %u. Offset = %d\n", mesh.getNodeTime(),offset);
}

void setup() {
  Wire.begin();
  Serial.begin(115200);
  Serial2.begin(9600);
  dht.begin();
  rtc.begin();
//  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  display.begin(i2c_Address, true); 
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

//mesh.setDebugMsgTypes( ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE ); // all types on
  mesh.setDebugMsgTypes( ERROR | STARTUP );  // set before init() so that you can see startup messages
  mesh.init( MESH_PREFIX, MESH_PASSWORD, MESH_PORT );
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);
  mesh.setRoot(true);
  mesh.setContainsRoot(true);
}

void loop() {
  // If NodeId is still 0, try re-initializing the mesh network
  if (mesh.getNodeId() == 0) {
    mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);
  }
  // it will run the user scheduler as well
  mesh.update();

  DateTime now = rtc.now();
  VOLTAJE = pzem.voltage();
  CORRIENTE = pzem.current();
  POTENCIA = pzem.power();
  ENERGIA = pzem.energy();
  TEMPERATURA = dht.readTemperature();  
  HUMEDAD = dht.readHumidity(); 

  //ESTADO DE CONEXION DE SENSOR PZEM
  if (isnan(VOLTAJE)) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Error de conexion...");
    display.display();
    Serial.println("Error de conexion...");
    return;
  }

  // Fecha y hora
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(now.year(), DEC); display.print('/'); display.print(now.month(), DEC); display.print('/'); display.print(now.day(), DEC);
  display.setCursor(70, 0);
  display.print(now.hour(), DEC); display.print(':'); display.print(now.minute(), DEC); display.print(':'); display.println(now.second(), DEC);

  // Graficar los datos de energía
  int x = 2;
  int y = 12;
  int ancho = 124;
  int alto = 40;
  int margenSuperior = 3; // Espacio entre el límite superior y las barras
  int margenLateral = 2; // Espacio entre el límite lateral y las barras

  // Obtener el valor máximo dinámicamente para autoescalar
  float valorMaximo = obtenerValorMaximo();
  if (valorMaximo == 0) valorMaximo = 1;  // Evitar división por 0
  display.drawRect(x - margenLateral, y - margenSuperior, ancho + 2 * margenLateral, alto + margenSuperior, SH110X_WHITE);

  // Escalar los valores de energía para poder graficar con autoescala
  for (int i = 0; i < 6; i++) {
    float valor = energiaDatos[(indice - 1 - i + 6) % 6]; // Obtener los últimos seis datos en orden
    int altura = (int)((valor / valorMaximo) * (alto - margenSuperior)); // Escalar el valor al rango de la altura del gráfico
    display.fillRect(x + i * (ancho / 6) + margenLateral, y + alto - altura, (ancho / 6) - 2 * margenLateral, altura, SH110X_WHITE); // Dibujar con margen lateral
  }
  if (condition_met() && !dataSent) {
    // Actualizar el arreglo de datos de energía
    energiaDatos[indice] = ENERGIA;
    indice = (indice + 1) % 6; // Ciclar el índice para mantener solo los últimos cinco datos

    dataSent = true; // Set the flag to true after sending data

    // Crear un buffer para los datos a enviar
    String dataBuffer = "";
    // Agregar datos locales primero
    dataBuffer += String(now.year()) + "-" + String(now.month()) + "-" + String(now.day()) + " ";
    dataBuffer += String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second()) + ",";
    dataBuffer += String(mesh.getNodeId()) + ",";   // ID local
    dataBuffer += "Nodo_Central,";                  // Nombre del central
    dataBuffer += String(ENERGIA, 2) + ",";
    dataBuffer += String(TEMPERATURA, 2) + ",";
    dataBuffer += String(HUMEDAD) + ",";

    // Remover la última coma
    if (dataBuffer.endsWith(",")) {
      dataBuffer.remove(dataBuffer.length() - 1);
    }

    // Enviar el buffer por I2C
    Wire.beginTransmission(I2C_SLAVE_ADDRESS);
    Wire.write((const uint8_t*)dataBuffer.c_str(), dataBuffer.length());
    Wire.endTransmission();

    Serial.println("Datos enviados:");
    Serial.println(dataBuffer);
  }
  // Mostrar energía, temperatura y humedad
  display.setCursor(0, y + alto + 5);
  display.print("E:"); display.print(ENERGIA); display.print("Wh ");  // Espacio entre cada dato
  display.print("T:"); display.print((int)TEMPERATURA); display.write(247); display.print("C ");  // Más espacio
  display.print("H:"); display.print(HUMEDAD); display.print("%");

  display.display();
  
  // ==== REPORTE DE TASA DE TRANSMISIÓN ====
  if (millis() - lastRateTime >= 1000) {
      Serial.printf("Tasa RX: %d paquetes/s, %d bytes/s\n",
                    packetsReceived, bytesReceived);

      packetsReceived = 0;
      bytesReceived = 0;
      lastRateTime = millis();
  }

  delay(1000);  // Esperar 1 segundo antes de refrescar la pantalla
}
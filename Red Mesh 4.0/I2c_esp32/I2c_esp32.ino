#include <Wire.h>
#include <SdFat.h>
#include <SPI.h>
#include <ThingSpeak.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define I2C_ADDRESS 0x08

// Configuración del módulo SD
#define SD_CS_PIN 5
SdFat SD;
SdFile dataFile;

// Configuración de redes WiFi
const char* ssid[] = {"WIFI-UPTC", "TECNO CAMON 30S PRO", "RED_TERCIARIA"};
const char* password[] = {"", "12345678", "contraseña_terciaria"};
const int cantidadRedes = 3;
WiFiClient client;

// Configuración de ThingSpeak
unsigned long channelNumber = 3084106;
const char* apiKey = "RWZ2Z530H2ZVYCXC";

// Buffer de datos recibidos por I2C
char dataBuffer[512]; 
volatile bool dataReceived = false;

// ====== MAPEO DE NODOS A FIELDS ======
struct NodoMap {
  String nombreNodo;
  int fieldNumber;
};

NodoMap nodoMap[] = {
  {"Nodo_Central", 1},
  {"Nodo_1", 2},
  {"Nodo_2", 3},
  {"Nodo_3", 4}
};

const int totalNodos = sizeof(nodoMap) / sizeof(nodoMap[0]);

// ====== VARIABLES GLOBALES ======
float energias[4] = {NAN, NAN, NAN, NAN};  // Energías de Nodo_Central, Nodo_1, Nodo_2, Nodo_3

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_ADDRESS);

  // Conectar a WiFi
  conectarWiFi();
  ThingSpeak.begin(client);

  // Inicializar SD
  if (!SD.begin(SD_CS_PIN, SD_SCK_MHZ(4))) {
    Serial.println("Error al inicializar la tarjeta SD");
    return;
  }
  Serial.println("Tarjeta SD inicializada correctamente");

  // Crear archivos con cabecera si no existen
  if (!SD.exists("nodos.csv")) {
    dataFile.open("nodos.csv", O_CREAT | FILE_WRITE);
    if (dataFile) {
      dataFile.println("ID_Nodo,Nombre_Nodo");
      dataFile.close();
      Serial.println("Archivo nodos.csv creado con cabecera");
    }
  }
  if (!SD.exists("datos.csv")) {
    dataFile.open("datos.csv", O_CREAT | FILE_WRITE);
    if (dataFile) {
      dataFile.println("ID_Nodo,Fecha,Hora,Energia,Temperatura,Humedad");
      dataFile.close();
      Serial.println("Archivo datos.csv creado con cabecera");
    }
  }
  if (!SD.exists("test.csv")) {
    dataFile.open("test.csv", O_CREAT | FILE_WRITE);
    if (dataFile) {
      dataFile.println("Fecha,Hora,ID_Nodo,Nombre_Nodo,TxRate,Latencia,Estabilidad,RSSI");
      dataFile.close();
      Serial.println("Archivo test.csv creado con cabecera");
    }
  }


  // Configurar recepción I2C
  Wire.onReceive(receiveData);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED || !checkInternetConnection()) {
    conectarWiFi();
  }

  if (dataReceived) {
    dataReceived = false;
    if (strlen(dataBuffer) > 0) {
      String dataString(dataBuffer);
      Serial.println("Datos recibidos: " + dataString);
      procesarDatos(dataString);
    }
  }

  delay(100);
}

// ====== RECEPCIÓN I2C ======
void receiveData(int byteCount) {
  int index = 0;
  while (Wire.available() && index < sizeof(dataBuffer) - 1) {
    dataBuffer[index++] = Wire.read();
  }
  dataBuffer[index] = '\0';
  dataReceived = true;
}

// ====== PROCESAR DATOS ======
void procesarDatos(String data) {
  // ====== DETECCIÓN DE PAQUETE DE TEST ======
  if (data.indexOf(",TEST,") != -1) {
      guardarTest(data);
      return;
  }

  // Formato esperado: YYYY-MM-DD HH:MM:SS,ID_NODO,NombreNodo,ENERGIA,TEMPERATURA,HUMEDAD
  int c1 = data.indexOf(',');
  if (c1 == -1) return;
  String fechaHora = data.substring(0, c1);

  int espacio = fechaHora.indexOf(' ');
  String fecha = fechaHora.substring(0, espacio);
  String hora = fechaHora.substring(espacio + 1);

  String resto = data.substring(c1 + 1);

  int c2 = resto.indexOf(',');
  if (c2 == -1) return;
  String idNodo = resto.substring(0, c2);

  resto = resto.substring(c2 + 1);
  int c3 = resto.indexOf(',');
  if (c3 == -1) return;
  String nombreNodo = resto.substring(0, c3);

  resto = resto.substring(c3 + 1);
  int c4 = resto.indexOf(',');
  if (c4 == -1) return;
  float energia = resto.substring(0, c4).toFloat();

  resto = resto.substring(c4 + 1);
  int c5 = resto.indexOf(',');
  if (c5 == -1) return;
  float temperatura = resto.substring(0, c5).toFloat();

  resto = resto.substring(c5 + 1);
  int c6 = resto.indexOf(',');
  String humedadStr;
  if (c6 == -1) {
    humedadStr = resto;
  } else {
    humedadStr = resto.substring(0, c6);
  }
  int humedad = humedadStr.toInt();

  // Guardar nodo en nodos.csv si no existe
  if (!nodoExiste("nodos.csv", idNodo)) {
    dataFile.open("nodos.csv", FILE_WRITE);
    if (dataFile) {
      dataFile.print(idNodo);
      dataFile.print(",");
      dataFile.println(nombreNodo);
      dataFile.close();
    }
  }

  // Guardar datos en datos.csv
  dataFile.open("datos.csv", FILE_WRITE);
  if (dataFile) {
    dataFile.print(idNodo); dataFile.print(",");
    dataFile.print(fecha); dataFile.print(",");
    dataFile.print(hora); dataFile.print(",");
    dataFile.print(energia, 2); dataFile.print(",");
    dataFile.print(temperatura, 2); dataFile.print(",");
    dataFile.println(humedad);
    dataFile.close();
  }

  // Actualizar buffer de energías
  for (int i = 0; i < totalNodos; i++) {
    if (nombreNodo == nodoMap[i].nombreNodo) {
      energias[i] = energia;
      break;
    }
  }

  // 🔹 Enviar cuando llega el Nodo_Central
  if (nombreNodo == "Nodo_Central") {
    Serial.println("Dato del Nodo_Central recibido, enviando a ThingSpeak...");
    enviarThingSpeak();
    resetBuffers();
  }
}

// ====== ENVÍO A THINGSPEAK ======
void enviarThingSpeak() {
  for (int i = 0; i < totalNodos; i++) {
    float valor = isnan(energias[i]) ? 0.0 : energias[i];  // Si no hay dato, usar 0.0
    ThingSpeak.setField(nodoMap[i].fieldNumber, valor);

    Serial.print("Nodo "); Serial.print(nodoMap[i].nombreNodo);
    Serial.print(" -> Field "); Serial.print(nodoMap[i].fieldNumber);
    Serial.print(" = "); Serial.println(valor);
  }

  int response = ThingSpeak.writeFields(channelNumber, apiKey);
  if (response == 200) {
    Serial.println("✅ Datos enviados a ThingSpeak correctamente.");
  } else {
    Serial.print("⚠️ Error al enviar a ThingSpeak: ");
    Serial.println(response);
  }
}

// ====== REINICIAR BUFFERS ======
void resetBuffers() {
  for (int i = 0; i < totalNodos; i++) {
    energias[i] = NAN;
  }
  Serial.println("Buffers reiniciados.");
}

// ====== UTILIDADES ======
bool nodoExiste(const char* filename, String idNodo) {
  SdFile file;
  if (!file.open(filename, FILE_READ)) {
    return false;
  }
  char line[50];
  while (file.fgets(line, sizeof(line))) {
    String linea(line);
    if (linea.startsWith(idNodo + ",")) {
      file.close();
      return true;
    }
  }
  file.close();
  return false;
}

void conectarWiFi() {
  for (int i = 0; i < cantidadRedes; i++) {
    WiFi.begin(ssid[i], password[i]);
    int intentos = 0;
    while (WiFi.status() != WL_CONNECTED && intentos < 5) {
      delay(1000);
      intentos++;
    }
    if (WiFi.status() == WL_CONNECTED && checkInternetConnection()) {
      Serial.println("Conectado a WiFi con acceso a Internet");
      Serial.print("Ip: ");
      Serial.println(WiFi.localIP());
      return;
    }
    WiFi.disconnect();
  }
  Serial.println("No se pudo conectar a ninguna red WiFi.");
}

bool checkInternetConnection() {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.begin("http://www.google.com");
  int httpCode = http.GET();
  http.end();
  return (httpCode > 0);
}

void guardarTest(String data) {

  // Formato:
  // YYYY-MM-DD HH:MM:SS,TEST,TxRate,Latencia,Estabilidad,RSSI

  // 1️⃣ Extraer fecha y hora
  int c1 = data.indexOf(',');
  String fechaHora = data.substring(0, c1);

  int espacio = fechaHora.indexOf(' ');
  String fecha = fechaHora.substring(0, espacio);
  String hora  = fechaHora.substring(espacio + 1);

  // 2️⃣ Extraer resto del paquete
  String resto = data.substring(c1 + 1);     // TEST,TxRate,Latencia,Estabilidad,RSSI

  // Quitar "TEST,"
  resto = resto.substring(5);                // TxRate,Latencia,Estabilidad,RSSI

  // 3️⃣ Parsear métricas
  int c2 = resto.indexOf(',');
  float txRate = resto.substring(0, c2).toFloat();

  resto = resto.substring(c2 + 1);
  int c3 = resto.indexOf(',');
  float latencia = resto.substring(0, c3).toFloat();

  resto = resto.substring(c3 + 1);
  int c4 = resto.indexOf(',');
  float estabilidad = resto.substring(0, c4).toFloat();

  resto = resto.substring(c4 + 1);
  int rssi = resto.toInt();

  // 4️⃣ Guardar en test.csv
  dataFile.open("test.csv", FILE_WRITE);
  if (dataFile) {
    dataFile.print(fecha);         dataFile.print(",");
    dataFile.print(hora);          dataFile.print(",");
    dataFile.print("TEST");        dataFile.print(",");
    dataFile.print("PruebasMesh"); dataFile.print(",");
    dataFile.print(txRate);        dataFile.print(",");
    dataFile.print(latencia);      dataFile.print(",");
    dataFile.print(estabilidad);   dataFile.print(",");
    dataFile.println(rssi);
    dataFile.close();
  }

  Serial.println("📊 Datos de TEST guardados en test.csv");
}


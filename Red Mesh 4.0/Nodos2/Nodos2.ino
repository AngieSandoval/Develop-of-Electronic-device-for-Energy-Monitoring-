#include "painlessMesh.h"
#include <PZEM004Tv30.h>
#include <DHT.h>
#include <RTClib.h>                 
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =============================
// Configuración del nodo
// =============================
#define NODE_NAME "Nodo_3"   // 🔹 Nombre personalizado del nodo

// =============================
// Configuracion red Mesh
// =============================
#define   MESH_PREFIX     "whateverYouLike"
#define   MESH_PASSWORD   "somethingSneaky"
#define   MESH_PORT       5555
Scheduler userScheduler; 
painlessMesh  mesh;

// =============================
// CONFIGURACION DE PANTALLA OLED SSD1306
// =============================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// =============================
// Configuración del PZEM
// =============================
PZEM004Tv30 pzem(Serial2, 16, 17);
float VOLTAJE, CORRIENTE, POTENCIA, ENERGIA, TEMPERATURA;
int HUMEDAD;
char mensaje[120];   

// =============================
// CONFIGURACION PARA DHT11
// =============================
#define DPIN 4        
#define DTYPE DHT11   
DHT dht(DPIN, DTYPE);

// =============================
// CONFIGURACION RTC
// =============================
RTC_DS3231 rtc;
char daysOfWeek[7][12] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

// =============================
// Scheduler task
// =============================
void sendMessage() ; 
Task taskSendMessage( TASK_SECOND * 1 , TASK_FOREVER, &sendMessage );

bool dataSent = false; 
float energiaDatos[6] = {0}; 
int indice = 0;

// =============================
// Funciones auxiliares
// =============================
bool condition_met() {
  int currentMinute = rtc.now().minute();
  if (currentMinute % 10 == 9) { 
    return true;
  } else {
    dataSent = false; 
    return false;
  }
}

float obtenerValorMaximo() {
  float maximo = energiaDatos[0];
  for (int i = 1; i < 6; i++) {
    if (energiaDatos[i] > maximo) {
      maximo = energiaDatos[i];
    }
  }
  return maximo;
}

// =============================
// Envío de mensaje
// =============================
void sendMessage() {
  if (condition_met() && !dataSent) {
    snprintf(mensaje, sizeof(mensaje), "%u,%s,%.2f,%.2f,%d", 
             mesh.getNodeId(), NODE_NAME, ENERGIA, TEMPERATURA, HUMEDAD);

    Serial.println("Datos enviados: ");
    Serial.println(mensaje);

    String msg = mensaje;
    mesh.sendBroadcast(msg);

    energiaDatos[indice] = ENERGIA;
    indice = (indice + 1) % 6; 

    dataSent = true;
  }
}

// =============================
// Callbacks de painlessMesh
// =============================
void receivedCallback( uint32_t from, String &msg ) {
  Serial.printf("startHere: Received from %u msg=%s\n", from, msg.c_str());
}

void newConnectionCallback(uint32_t nodeId) {
    Serial.printf("--> startHere: New Connection, nodeId = %u\n", nodeId);
}

void changedConnectionCallback() {
  Serial.printf("Changed connections\n");
}

void nodeTimeAdjustedCallback(int32_t offset) {
    Serial.printf("Adjusted time %u. Offset = %d\n", mesh.getNodeTime(),offset);
}

// =============================
// Setup
// =============================
void setup() {
  Serial.begin(115200);
  dht.begin();
  rtc.begin();

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("No se encuentra la OLED SSD1306"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  mesh.setDebugMsgTypes( ERROR | STARTUP );  
  mesh.init( MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT );
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  userScheduler.addTask( taskSendMessage );
  taskSendMessage.enable();
}

// =============================
// Loop
// =============================
void loop() {
  if (mesh.getNodeId() == 0) {
    mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  }
  mesh.update();

  DateTime now = rtc.now();
  VOLTAJE = pzem.voltage();
  CORRIENTE = pzem.current();
  POTENCIA = pzem.power();
  ENERGIA = pzem.energy();
  TEMPERATURA = dht.readTemperature();  
  HUMEDAD = dht.readHumidity(); 

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

  // Gráfico de energía
  int x = 2, y = 12, ancho = 124, alto = 40;
  int margenSuperior = 3, margenLateral = 2; 

  float valorMaximo = obtenerValorMaximo();
  if (valorMaximo == 0) valorMaximo = 1;  
  display.drawRect(x - margenLateral, y - margenSuperior, ancho + 2 * margenLateral, alto + margenSuperior, SSD1306_WHITE);

  for (int i = 0; i < 6; i++) {
    float valor = energiaDatos[(indice - 1 - i + 6) % 6]; 
    int altura = (int)((valor / valorMaximo) * (alto - margenSuperior)); 
    display.fillRect(x + i * (ancho / 6) + margenLateral, y + alto - altura, (ancho / 6) - 2 * margenLateral, altura, SSD1306_WHITE); 
  }

  // Mostrar energía, temperatura y humedad
  display.setCursor(0, y + alto + 5);
  display.print("E:"); display.print(ENERGIA); display.print("Wh ");  
  display.print("T:"); display.print((int)TEMPERATURA); display.write(247); display.print("C ");  
  display.print("H:"); display.print(HUMEDAD); display.print("%");

  display.display();
  
  delay(1000);
}
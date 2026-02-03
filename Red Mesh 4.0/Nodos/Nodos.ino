#include "painlessMesh.h"
#include <PZEM004Tv30.h>
#include <DHT.h>
#include <RTClib.h>                 
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <math.h>

float energiaHist[3] = {0, 0, 0};   // E(t), E(t-1), E(t-2)
float dE1 = 0, dE2 = 0, dE3 = 0;

float salidaIA = 0.0;
bool alertaIA = false;

#define UMBRAL_IA 0.8


// =============================
// Configuración del nodo
// =============================
#define NODE_NAME "Nodo_1"   // 🔹 Nombre personalizado del nodo

// =============================
// Configuracion red Mesh
// =============================
#define   MESH_PREFIX     "whateverYouLike"
#define   MESH_PASSWORD   "somethingSneaky"
#define   MESH_PORT       5555
Scheduler userScheduler; // to control your personal task
painlessMesh  mesh;

// =============================
// CONFIGURACION DE PANTALLA OLED Grande
// =============================
#define i2c_Address 0x3c
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// =============================
// Configuración del PZEM
// =============================
PZEM004Tv30 pzem(Serial2, 16, 17);
float VOLTAJE, CORRIENTE, POTENCIA, ENERGIA, TEMPERATURA;
int HUMEDAD;
char mensaje[120];   // Amplié un poco el tamaño para incluir el nombre

// =============================
// CONFIGURACION PARA DHT11
// =============================
#define DPIN 4        // Pin to connect DHT sensor (GPIO number)
#define DTYPE DHT11   // Define DHT 11 or DHT22 sensor type
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
void sendMessage() ; // Prototype
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
    // 🔹 Empaquetar los datos en un mensaje con ID + Nombre
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

/* =============================
   RED NEURONAL (RED2)
   ============================= */

// MapMinMax
static const float xoffset[3] = {1.0f, 0.01f, 0.0f};
static const float gain[3]    = {0.66666667f, 0.05863383f, 0.09625668f};
static const float ymin = -1.0f;

// Capa 1
static const float b1[10] = {
  8.99667813f, -2.36740847f, 0.05901254f, -0.50787921f,
  0.01110041f, -0.71881676f, -0.93111970f, -0.19904440f,
 -2.88271702f, -2.00480288f
};

static const float IW1_1[10][3] = {
 { 0.08628365f, -0.07537807f,  9.96578116f},
 { 2.02836888f, -2.13374115f, -0.15451130f},
 { 0.09267377f,  2.43548941f, -2.65060721f},
 { 2.86364424f,  0.96932403f,  1.44389684f},
 {-0.70722450f,  2.93735735f, -0.72344443f},
 { 0.06278112f,  2.94387347f,  0.41990963f},
 { 0.35946351f, -2.44368030f,  1.59841692f},
 {-3.69534713f,  0.25981019f,  2.75465859f},
 {-1.46709338f,  2.04738087f,  0.63749149f},
 {-2.12634484f, -1.01433281f, -3.99821373f}
};

// Capa 2
static const float b2 = 2.15254164f;
static const float LW2_1[10] = {
 13.91143902f, 0.29027833f, -0.88505224f, 0.49413783f,
  0.85573717f,-0.57316007f,  0.55229180f, 2.16638065f,
 -1.19413452f,-1.58926661f
};

static inline float tansig(float x) {
  return 2.0f / (1.0f + expf(-2.0f * x)) - 1.0f;
}

static inline float logsig(float x) {
  return 1.0f / (1.0f + expf(-x));
}

float RED2_predict(float energia, float temp, float hum) {
  float x[3] = {energia, temp, hum};
  float Xp[3];

  for (int i = 0; i < 3; i++)
    Xp[i] = (x[i] - xoffset[i]) * gain[i] + ymin;

  float a1[10];
  for (int i = 0; i < 10; i++) {
    float sum = b1[i];
    for (int j = 0; j < 3; j++)
      sum += IW1_1[i][j] * Xp[j];
    a1[i] = tansig(sum);
  }

  float sum = b2;
  for (int i = 0; i < 10; i++)
    sum += LW2_1[i] * a1[i];

  return logsig(sum);
}

// =============================
// Setup
// =============================
void setup() {
  Serial.begin(115200);
  dht.begin();
  rtc.begin();
  display.begin(i2c_Address, true); 
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

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

  // Desplazar historial
  energiaHist[2] = energiaHist[1];
  energiaHist[1] = energiaHist[0];
  energiaHist[0] = ENERGIA;

  // Calcular diferenciales (solo si hay datos válidos)
  dE1 = energiaHist[0] - energiaHist[1];
  dE2 = energiaHist[1] - energiaHist[2];
  dE3 = (dE1 + dE2) / 2.0;

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
  display.drawRect(x - margenLateral, y - margenSuperior, ancho + 2 * margenLateral, alto + margenSuperior, SH110X_WHITE);

  for (int i = 0; i < 6; i++) {
    float valor = energiaDatos[(indice - 1 - i + 6) % 6]; 
    int altura = (int)((valor / valorMaximo) * (alto - margenSuperior)); 
    display.fillRect(x + i * (ancho / 6) + margenLateral, y + alto - altura, (ancho / 6) - 2 * margenLateral, altura, SH110X_WHITE); 
  }

  // Mostrar energía, temperatura y humedad
  display.setCursor(0, y + alto + 5);
  display.print("E:"); display.print(ENERGIA); display.print("Wh ");  
  display.print("T:"); display.print((int)TEMPERATURA); display.write(247); display.print("C ");  
  display.print("H:"); display.print(HUMEDAD); display.print("%");

  salidaIA = RED2_predict(dE1, dE2, dE3);
  alertaIA = (salidaIA >= UMBRAL_IA);

  if (alertaIA) {
    display.clearDisplay();
    
    display.fillRect(0, 25, 128, 19, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(1, 27);
    display.println("ALERTA:");
    display.setCursor(1, 35);
    display.println("ANOMALIA DETECTADA!");
    display.setTextColor(SH110X_WHITE);
  }

  display.display();

  delay(1000);
}

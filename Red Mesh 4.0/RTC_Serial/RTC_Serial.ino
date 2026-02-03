#include <RTClib.h>
#include <Wire.h>

// CONFIGURACION RTC
RTC_DS3231 rtc;
char daysOfWeek[7][12] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

void setup() {
  Serial.begin(115200);
  rtc.begin();

  // Ajusta la fecha/hora con la del compilador (solo la primera vez)
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
}

void loop() {
  DateTime now = rtc.now();

  // Enviar fecha y hora al monitor serial
  Serial.print(now.year(), DEC);
  Serial.print('/');
  Serial.print(now.month(), DEC);
  Serial.print('/');
  Serial.print(now.day(), DEC);
  Serial.print("  ");
  Serial.print(now.hour(), DEC);
  Serial.print(':');
  Serial.print(now.minute(), DEC);
  Serial.print(':');
  Serial.println(now.second(), DEC);

  delay(1000);  // Esperar 1 segundo antes de actualizar
}

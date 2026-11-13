#include <SoftwareSerial.h>
#include <PZEM004T_V1.h>
#include <Wire.h>
#include <RTClib.h> // Installer "RTClib" (Adafruit) dans le gestionnaire de bibliothèques.
#include <EnergyStorage.h>

SoftwareSerial pzemPort(10, 11);
PZEM004T_V1 pzem(pzemPort, 0x01);
RTC_DS3231 rtc;
EnergyStorage memory(Wire, false); // EEPROM 24C08, A2 à GND.

unsigned long lastSampleMs = 0;

void setup() {
  Serial.begin(115200);
  pzem.begin(9600);
  Wire.begin();
  if (!rtc.begin()) {
    Serial.println(F("ERREUR : DS3231 introuvable"));
    while (true) delay(1000);
  }
  if (rtc.lostPower()) {
    Serial.println(F("ERREUR : horloge DS3231 non fiable ; regler l'heure"));
    while (true) delay(1000);
  }
  EnergyStorageStatus st = memory.begin();
  if (st != ES_OK) {
    Serial.print(F("ERREUR EEPROM : "));
    Serial.println((int)st);
    while (true) delay(1000);
  }
  Serial.println(F("Compteur pret. EEPROM : historique energetique uniquement."));
}

void loop() {
  if (millis() - lastSampleMs < 1000) return;
  lastSampleMs = millis();
  if (!pzem.read()) {
    Serial.println(pzem.getErrorMessage());
    return;
  }
  DateTime now = rtc.now();
  if (!now.isValid()) {
    Serial.println(F("Horodatage RTC invalide"));
    return;
  }
  // Note : PZEM004T_V1 v0.1.0 fournit l'energie en float.
  float wh = pzem.energyWh();
  if (isnan(wh) || wh < 0 || wh >= 4294967040.0f) return;
  uint32_t cumulativeWh = (uint32_t)wh;
  EnergyStorageStatus status = memory.update(now.year(), now.month(), now.day(),
                                               now.hour(), now.minute(), cumulativeWh);
  if (status != ES_OK) {
    Serial.print(F("Erreur EnergyStorage : "));
    Serial.println((int)status);
    return;
  }
  Serial.print(F("Tension : ")); Serial.println(pzem.voltage());
  Serial.print(F("Courant : ")); Serial.println(pzem.current());
  Serial.print(F("Energie jour (Wh) : ")); Serial.println(memory.todayWh());
  Serial.print(F("Energie mois (Wh) : ")); Serial.println(memory.monthWh());

  // L'application ESP32 transmet les valeurs instantanees au Cloud.
  // Le Cloud archive UNIQUEMENT l'energie ; aucun HTTP ici.
}

#ifndef ENERGY_STORAGE_H
#define ENERGY_STORAGE_H

#include <Arduino.h>
#include <Wire.h>

// Mémoire externe AT24C08 / 24LC08 (1024 octets, pages de 16 octets).
// L'horloge (DS3231) et les communications Cloud sont gérées par l'application.
// Ne sauvegarde JAMAIS tension, courant, puissance, fréquence ni facteur PF.

enum EnergyStorageStatus : uint8_t {
  ES_OK = 0,
  ES_NO_EEPROM,
  ES_IO_ERROR,
  ES_INVALID_DATE,
  ES_CLOCK_BACKWARDS,
  ES_COUNTER_RESET,
  ES_NOT_READY
};

enum EnergyRecordQuality : uint8_t {
  ES_DAY_COMPLETE = 1,  // Mesures suivies jusqu'à la fin du jour (tolérance 5 min)
  ES_DAY_PARTIAL  = 2   // Démarrage tardif, arrêt prolongé ou trou temporel
};

struct EnergyDayRecord {
  uint16_t dayKey;       // Nombre de jours depuis le 01/01/2000
  uint32_t startWh;      // Compteur PZEM au début de la période suivie
  uint32_t endWh;        // Dernier compteur PZEM du jour
  uint32_t energyWh;     // endWh - startWh
  uint8_t quality;       // ES_DAY_COMPLETE ou ES_DAY_PARTIAL
};

class EnergyStorage {
public:
  // Pour une puce dont A2 = GND, utiliser a2High = false.
  explicit EnergyStorage(TwoWire &wire = Wire, bool a2High = false);

  // Charge le dernier point de reprise ; n'efface jamais la mémoire.
  EnergyStorageStatus begin();

  // Appeler après une lecture PZEM valide et une heure DS3231 vérifiée.
  // La date/heure doit être locale, monotone ; minute 0..59.
  EnergyStorageStatus update(uint16_t year, uint8_t month, uint8_t day,
                             uint8_t hour, uint8_t minute, uint32_t totalWh);

  bool ready() const { return _ready; }
  uint32_t todayWh() const { return _ready ? _lastWh - _dayStartWh : 0; }
  uint32_t monthWh() const { return _ready ? _lastWh - _monthStartWh : 0; }
  uint32_t totalWh() const { return _ready ? _lastWh : 0; }
  bool todayPartial() const { return _dayPartial; }

  // Jours terminés des 7 derniers jours : tri chronologique croissant.
  // maxRecords <= 7 ; les jours non mesurés ne sont jamais inventés.
  uint8_t getHistory(EnergyDayRecord *out, uint8_t maxRecords);
  static bool decodeDayKey(uint16_t key, uint16_t &year, uint8_t &month, uint8_t &day);

private:
  static const uint16_t EEPROM_BYTES = 1024;
  static const uint16_t DAY_OFFSET = 64;
  static const uint8_t DAY_SLOTS = 7;
  static const uint8_t DAY_BYTES = 32;
  static const uint16_t CHECKPOINT_OFFSET = 288;
  static const uint8_t CHECKPOINT_SLOTS = 24;
  static const uint8_t CHECKPOINT_BYTES = 24;
  static const uint16_t CHECKPOINT_MAGIC = 0xE5C1;
  static const uint16_t DAY_MAGIC = 0xE5D1;

  TwoWire &_wire;
  bool _a2High;
  bool _ready;
  bool _dayPartial;
  uint16_t _dayKey;
  uint16_t _lastMinute;
  uint16_t _savedBucket;
  uint32_t _dayStartWh;
  uint32_t _monthStartWh;
  uint32_t _lastWh;
  uint32_t _seq;
  uint8_t _nextCheckpoint;

  uint8_t i2cAddress(uint16_t memoryAddress) const;
  bool readBytes(uint16_t address, uint8_t *dest, uint8_t count);
  bool writeBytes(uint16_t address, const uint8_t *src, uint8_t count);
  bool writeCheckpoint(uint16_t dayKey, uint16_t minuteOfDay,
                       uint32_t dayStartWh, uint32_t monthStartWh,
                       uint32_t lastWh, bool dayPartial);
  bool saveDay(uint16_t dayKey, uint32_t startWh, uint32_t endWh,
               uint8_t quality);
  bool readDay(uint8_t slot, EnergyDayRecord &record);
  static bool validDate(uint16_t year, uint8_t month, uint8_t day);
  static uint16_t dateKey(uint16_t year, uint8_t month, uint8_t day);
  static uint16_t monthId(uint16_t dayKey);
  static uint16_t crc16(const uint8_t *data, uint8_t length);
  static uint16_t get16(const uint8_t *p);
  static uint32_t get32(const uint8_t *p);
  static void put16(uint8_t *p, uint16_t v);
  static void put32(uint8_t *p, uint32_t v);
};

#endif

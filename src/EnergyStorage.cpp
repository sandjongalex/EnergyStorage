#include "EnergyStorage.h"
#include <string.h>

EnergyStorage::EnergyStorage(TwoWire &wire, bool a2High)
  : _wire(wire), _a2High(a2High), _ready(false), _dayPartial(true),
    _dayKey(0), _lastMinute(0), _savedBucket(0), _dayStartWh(0),
    _monthStartWh(0), _lastWh(0), _seq(0), _nextCheckpoint(0) {}

uint16_t EnergyStorage::get16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
uint32_t EnergyStorage::get32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
void EnergyStorage::put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
void EnergyStorage::put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
uint16_t EnergyStorage::crc16(const uint8_t *data, uint8_t length) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
  }
  return crc;
}

// 24C08 : 4 blocs de 256 octets (adresses 7 bits 0x50..0x53).
// A2 haut : 0x54..0x57. A1/A0 ne sélectionnent pas le dispositif.
uint8_t EnergyStorage::i2cAddress(uint16_t memoryAddress) const {
  return (uint8_t)(0x50 | (_a2High ? 0x04 : 0) | ((memoryAddress >> 8) & 0x03));
}

bool EnergyStorage::readBytes(uint16_t address, uint8_t *dest, uint8_t count) {
  if ((uint32_t)address + count > EEPROM_BYTES || (!dest && count)) return false;
  while (count) {
    uint8_t chunk = count > 16 ? 16 : count;
    uint8_t availableInBlock = (uint8_t)(256U - (address & 0xFF));
    // 0 signifie ici 256 octets restants (adresse basse 0).
    if (availableInBlock != 0 && chunk > availableInBlock) chunk = availableInBlock;
    uint8_t dev = i2cAddress(address);
    _wire.beginTransmission(dev);
    _wire.write((uint8_t)(address & 0xFF));
    if (_wire.endTransmission(false) != 0) return false;
    if (_wire.requestFrom(dev, chunk) != chunk) return false;
    for (uint8_t i = 0; i < chunk; ++i) {
      if (!_wire.available()) return false;
      dest[i] = (uint8_t)_wire.read();
    }
    dest += chunk;
    address += chunk;
    count -= chunk;
  }
  return true;
}

bool EnergyStorage::writeBytes(uint16_t address, const uint8_t *src, uint8_t count) {
  if ((uint32_t)address + count > EEPROM_BYTES || (!src && count)) return false;
  while (count) {
    uint8_t remainInPage = (uint8_t)(16 - (address & 0x0F));
    uint8_t chunk = count < remainInPage ? count : remainInPage;
    uint8_t dev = i2cAddress(address);
    _wire.beginTransmission(dev);
    _wire.write((uint8_t)(address & 0xFF));
    for (uint8_t i = 0; i < chunk; ++i) _wire.write(src[i]);
    if (_wire.endTransmission() != 0) return false;
    // ACK polling : temps d'écriture EEPROM <= 5 ms selon la fiche Microchip.
    bool acknowledged = false;
    unsigned long start = millis();
    do {
      _wire.beginTransmission(dev);
      if (_wire.endTransmission() == 0) { acknowledged = true; break; }
      delay(1);
    } while (millis() - start < 15);
    if (!acknowledged) return false;
    src += chunk;
    count -= chunk;
    address += chunk;
  }
  return true;
}

static bool esLeap(uint16_t y) {
  return ((y % 4 == 0) && (y % 100 != 0 || y % 400 == 0));
}
static uint8_t esDaysInMonth(uint16_t y, uint8_t m) {
  static const uint8_t lengths[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  return (uint8_t)(lengths[m - 1] + (m == 2 && esLeap(y) ? 1 : 0));
}
bool EnergyStorage::validDate(uint16_t year, uint8_t month, uint8_t day) {
  return year >= 2000 && year <= 2099 && month >= 1 && month <= 12 &&
         day >= 1 && day <= esDaysInMonth(year, month);
}
uint16_t EnergyStorage::dateKey(uint16_t year, uint8_t month, uint8_t day) {
  uint16_t days = 0;
  for (uint16_t y = 2000; y < year; ++y) days += esLeap(y) ? 366 : 365;
  for (uint8_t m = 1; m < month; ++m) days += esDaysInMonth(year, m);
  return days + day - 1;
}
bool EnergyStorage::decodeDayKey(uint16_t key, uint16_t &year, uint8_t &month, uint8_t &day) {
  year = 2000;
  if (key >= 36525U) return false;  // 2000..2099, inclus
  while (year <= 2099) {
    uint16_t n = esLeap(year) ? 366 : 365;
    if (key < n) break;
    key -= n; ++year;
  }
  month = 1;
  while (month <= 12) {
    uint8_t n = esDaysInMonth(year, month);
    if (key < n) break;
    key -= n; ++month;
  }
  day = (uint8_t)(key + 1);
  return year <= 2099 && month <= 12;
}
uint16_t EnergyStorage::monthId(uint16_t key) {
  uint16_t year; uint8_t month, day;
  if (!decodeDayKey(key, year, month, day)) return 0;
  return (uint16_t)((year - 2000U) * 12U + month);
}

EnergyStorageStatus EnergyStorage::begin() {
  _wire.begin();
  _ready = false;
  _wire.beginTransmission(i2cAddress(0));
  if (_wire.endTransmission() != 0) return ES_NO_EEPROM;
  bool found = false;
  uint8_t bestSlot = 0;
  uint8_t buffer[CHECKPOINT_BYTES];
  uint32_t bestSeq = 0;
  for (uint8_t slot = 0; slot < CHECKPOINT_SLOTS; ++slot) {
    if (!readBytes(CHECKPOINT_OFFSET + (uint16_t)slot * CHECKPOINT_BYTES,
                   buffer, CHECKPOINT_BYTES)) return ES_IO_ERROR;
    if (get16(buffer) != CHECKPOINT_MAGIC ||
        get16(buffer + 22) != crc16(buffer, 22)) continue;
    uint16_t dayKey = get16(buffer + 6);
    uint16_t minuteFlags = get16(buffer + 20);
    if (dayKey >= 36525U || (minuteFlags & 0x7FFF) >= 1440U ||
        get32(buffer + 8) > get32(buffer + 16) ||
        get32(buffer + 12) > get32(buffer + 16)) continue;
    uint32_t seq = get32(buffer + 2);
    if (!found || (int32_t)(seq - bestSeq) > 0) {
      found = true; bestSeq = seq; bestSlot = slot;
    }
  }
  if (!found) {
    _nextCheckpoint = 0;
    _seq = 0;
    return ES_OK; // premier démarrage : update() initialise le point de reprise.
  }
  if (!readBytes(CHECKPOINT_OFFSET + (uint16_t)bestSlot * CHECKPOINT_BYTES,
                 buffer, CHECKPOINT_BYTES)) return ES_IO_ERROR;
  _seq = get32(buffer + 2);
  _dayKey = get16(buffer + 6);
  _dayStartWh = get32(buffer + 8);
  _monthStartWh = get32(buffer + 12);
  _lastWh = get32(buffer + 16);
  uint16_t minuteFlags = get16(buffer + 20);
  _lastMinute = minuteFlags & 0x7FFF;
  _dayPartial = (minuteFlags & 0x8000) != 0;
  _savedBucket = _lastMinute / 15;
  _nextCheckpoint = (uint8_t)((bestSlot + 1) % CHECKPOINT_SLOTS);
  _ready = true;
  return ES_OK;
}

bool EnergyStorage::writeCheckpoint(uint16_t dayKey, uint16_t minuteOfDay,
                                    uint32_t dayStartWh, uint32_t monthStartWh,
                                    uint32_t lastWh, bool dayPartial) {
  uint8_t buffer[CHECKPOINT_BYTES];
  memset(buffer, 0, sizeof(buffer));
  put16(buffer, CHECKPOINT_MAGIC);
  put32(buffer + 2, _seq + 1);
  put16(buffer + 6, dayKey);
  put32(buffer + 8, dayStartWh);
  put32(buffer + 12, monthStartWh);
  put32(buffer + 16, lastWh);
  put16(buffer + 20, (uint16_t)(minuteOfDay | (dayPartial ? 0x8000 : 0)));
  put16(buffer + 22, crc16(buffer, 22));
  uint16_t addr = CHECKPOINT_OFFSET + (uint16_t)_nextCheckpoint * CHECKPOINT_BYTES;
  if (!writeBytes(addr, buffer, CHECKPOINT_BYTES)) return false;
  uint8_t verify[CHECKPOINT_BYTES];
  if (!readBytes(addr, verify, CHECKPOINT_BYTES) ||
      memcmp(buffer, verify, CHECKPOINT_BYTES) != 0) return false;
  ++_seq;
  _nextCheckpoint = (uint8_t)((_nextCheckpoint + 1) % CHECKPOINT_SLOTS);
  _savedBucket = minuteOfDay / 15;
  return true;
}

bool EnergyStorage::readDay(uint8_t slot, EnergyDayRecord &record) {
  if (slot >= DAY_SLOTS) return false;
  uint8_t buffer[DAY_BYTES];
  if (!readBytes(DAY_OFFSET + (uint16_t)slot * DAY_BYTES, buffer, DAY_BYTES)) return false;
  if (get16(buffer) != DAY_MAGIC ||
      get16(buffer + 30) != crc16(buffer, 30)) return false;
  record.dayKey = get16(buffer + 6);
  record.startWh = get32(buffer + 8);
  record.endWh = get32(buffer + 12);
  record.energyWh = get32(buffer + 16);
  record.quality = buffer[20];
  return record.dayKey < 36525U && record.endWh >= record.startWh &&
         record.energyWh == record.endWh - record.startWh &&
         (record.quality == ES_DAY_COMPLETE || record.quality == ES_DAY_PARTIAL);
}

bool EnergyStorage::saveDay(uint16_t dayKey, uint32_t startWh,
                            uint32_t endWh, uint8_t quality) {
  uint8_t slot = dayKey % DAY_SLOTS;
  EnergyDayRecord existing;
  // Une clôture validée ne doit pas être réécrite après une coupure au milieu
  // de la transition de journée, avant la sauvegarde du point de reprise.
  if (readDay(slot, existing) && existing.dayKey == dayKey) return true;
  uint8_t buffer[DAY_BYTES];
  memset(buffer, 0, sizeof(buffer));
  put16(buffer, DAY_MAGIC);
  put32(buffer + 2, _seq + 1);
  put16(buffer + 6, dayKey);
  put32(buffer + 8, startWh);
  put32(buffer + 12, endWh);
  put32(buffer + 16, endWh - startWh);
  buffer[20] = quality;
  put16(buffer + 30, crc16(buffer, 30));
  uint16_t addr = DAY_OFFSET + (uint16_t)slot * DAY_BYTES;
  if (!writeBytes(addr, buffer, DAY_BYTES)) return false;
  uint8_t verify[DAY_BYTES];
  return readBytes(addr, verify, DAY_BYTES) &&
         memcmp(buffer, verify, DAY_BYTES) == 0;
}

EnergyStorageStatus EnergyStorage::update(uint16_t year, uint8_t month, uint8_t day,
                                          uint8_t hour, uint8_t minute, uint32_t totalWh) {
  if (!validDate(year, month, day) || hour > 23 || minute > 59) return ES_INVALID_DATE;
  uint16_t key = dateKey(year, month, day);
  uint16_t nowMinute = (uint16_t)hour * 60U + minute;
  if (!_ready) {
    // Première observation : la période en cours est partielle.
    if (!writeCheckpoint(key, nowMinute, totalWh, totalWh, totalWh, true)) return ES_IO_ERROR;
    _dayKey = key;
    _lastMinute = nowMinute;
    _dayStartWh = _monthStartWh = _lastWh = totalWh;
    _dayPartial = true;
    _ready = true;
    return ES_OK;
  }
  if (key < _dayKey || (key == _dayKey && nowMinute < _lastMinute))
    return ES_CLOCK_BACKWARDS;
  if (totalWh < _lastWh) return ES_COUNTER_RESET;

  if (key != _dayKey) {
    // Ne pas attribuer à l'ancien jour une consommation éventuellement faite
    // durant une interruption de l'ATmega. On clôt au dernier relevé connu.
    bool complete = !_dayPartial && key == (uint16_t)(_dayKey + 1) &&
                    _lastMinute >= 1435; // au moins 23h55
    if (!saveDay(_dayKey, _dayStartWh, _lastWh,
                 complete ? ES_DAY_COMPLETE : ES_DAY_PARTIAL)) return ES_IO_ERROR;
    bool gap = key != (uint16_t)(_dayKey + 1);
    bool partialToday = (nowMinute > 5) || gap;
    uint32_t nextMonthStart = monthId(key) == monthId(_dayKey) ? _monthStartWh : totalWh;
    if (!writeCheckpoint(key, nowMinute, totalWh, nextMonthStart,
                         totalWh, partialToday)) return ES_IO_ERROR;
    _dayKey = key;
    _dayStartWh = totalWh;
    _monthStartWh = nextMonthStart;
    _dayPartial = partialToday;
    _lastWh = totalWh;
    _lastMinute = nowMinute;
    return ES_OK;
  }

  _lastWh = totalWh;
  _lastMinute = nowMinute;
  if ((nowMinute / 15) != _savedBucket) {
    if (!writeCheckpoint(_dayKey, nowMinute, _dayStartWh,
                         _monthStartWh, _lastWh, _dayPartial)) return ES_IO_ERROR;
  }
  return ES_OK;
}

uint8_t EnergyStorage::getHistory(EnergyDayRecord *out, uint8_t maxRecords) {
  if (!_ready || !out || !maxRecords) return 0;
  if (maxRecords > DAY_SLOTS) maxRecords = DAY_SLOTS;
  EnergyDayRecord records[DAY_SLOTS];
  uint8_t count = 0;
  for (uint8_t slot = 0; slot < DAY_SLOTS; ++slot) {
    EnergyDayRecord rec;
    if (!readDay(slot, rec)) continue;
    if (rec.dayKey >= _dayKey || _dayKey - rec.dayKey > DAY_SLOTS) continue;
    uint8_t insert = 0;
    while (insert < count && records[insert].dayKey < rec.dayKey) ++insert;
    for (uint8_t j = count; j > insert; --j) records[j] = records[j - 1];
    records[insert] = rec;
    ++count;
  }
  uint8_t n = count < maxRecords ? count : maxRecords;
  for (uint8_t i = 0; i < n; ++i) out[i] = records[count - n + i];
  return n;
}

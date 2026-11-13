# EnergyStorage — bibliothèque Arduino (v0.1.0)

Bibliothèque de **stockage énergétique local** pour un compteur intelligent **ATmega328P + PZEM-004T V1.0 + RTC DS3231 + EEPROM 24C08**.

Cette bibliothèque est **séparée** de `PZEM004T_V1` : celle-ci continue de mesurer l'électricité, tandis que `EnergyStorage` ne reçoit et ne sauvegarde **que l'énergie cumulée en Wh**, la date, l'heure et les informations techniques de reprise. **Tension, courant, puissance, fréquence, facteur de puissance et alarme ne sont jamais archivés.**

## Fonctions de la version 0.1.0

- Historique circulaire des **7 derniers jours clôturés** : un relevé par journée, remplacé après rotation.
- Points de reprise persistants toutes les **15 minutes**, répartis sur **24 emplacements** (wear leveling simple).
- Calcul de la consommation **du jour** et **du mois** à partir du compteur d'énergie cumulé du PZEM.
- Relecture automatique du dernier point de reprise valide après redémarrage.
- CRC16 sur les enregistrements et relecture après écriture (détection de coupures pendant l'écriture).
- Détection de retour arrière de date/heure et de remise à zéro du compteur PZEM.
- Jours partiels signalés (`ES_DAY_PARTIAL`) ; aucune consommation inconnue n'est attribuée artificiellement à un autre jour.
- **Pas de client réseau** : le code Cloud reste dans le firmware de communication.

## Matériel et câblage

| Signal | Arduino Uno / ATmega328P |
|---|---|
| 24C08 SDA | A4 (SDA) |
| 24C08 SCL | A5 (SCL) |
| 24C08 VCC / GND | 5V / GND, si conforme à la référence du composant |
| 24C08 A2 | GND (`a2High=false`) |
| 24C08 WP | GND pour autoriser les écritures |
| DS3231 SDA / SCL | A4 / A5, même bus I²C |
| PZEM TX / RX / GND | D10 / D11 / GND selon le montage existant validé |

Vérifier le câblage des autres broches suivant la **fiche technique de la référence exacte** (plusieurs fabricants existent), ainsi que les résistances de tirage I²C. **Ne pas connecter directement un bus I²C à 5 V sur un ESP32 à 3,3 V** ; utiliser un convertisseur de niveau lorsque nécessaire.

Le code utilise la famille AT24C08/24LC08 avec mémoire de **1024 octets**, pages d'écriture de **16 octets** et adressage I²C par **4 blocs de 256 octets** (`0x50` à `0x53` quand A2 = 0). Ce n'est **pas** le protocole d'adressage à deux octets des 24C256/512.

## Carte mémoire EEPROM (1024 octets)

| Adresses | Allocation | Volume |
|---|---|---:|
| 0–63 | Réservé | 64 octets |
| 64–287 | 7 enregistrements journaliers × 32 octets | 224 octets |
| 288–863 | 24 points de reprise × 24 octets | 576 octets |
| 864–1023 | Réservé | 160 octets |
| **Total** | | **1024 octets** |

Les emplacements journaliers sont identifiés par le nombre de jours écoulés depuis le 01/01/2000 et sont réutilisés modulo 7. **Sept jours clôturés** peuvent être conservés, tandis que le jour courant reste dans l'état de reprise.

Les points de reprise circulent sur 24 emplacements. À 96 sauvegardes/jour, chaque emplacement reçoit en moyenne 4 écritures par jour (soit ~7 300 sur cinq ans), hors redémarrages et transitions. L'endurance reste à vérifier pour **la référence précise** et la température d'utilisation.

## Installation

Dans Arduino IDE : **Croquis > Inclure une bibliothèque > Ajouter la bibliothèque .ZIP…**, choisir `EnergyStorage_v0.1.0.zip`.

Pour exécuter `MonitoringAvecPZEM`, installer également :

- La bibliothèque existante **PZEM004T_V1 v0.1.0** ;
- **RTClib (Adafruit)**, via le gestionnaire de bibliothèques ;
- La bibliothèque **Wire**, fournie par Arduino.

L'horloge DS3231 doit être réglée avec une date et une heure correctes et conserver l'heure lors des coupures. L'exemple bloque les sauvegardes si `rtc.lostPower()` indique une horloge potentiellement non fiable. L'API `EnergyStorage` reste indépendante de RTClib : le programme lui transmet simplement date, heure et énergie.

## Utilisation minimale

```cpp
#include <Wire.h>
#include <EnergyStorage.h>

EnergyStorage energy;  // 24C08 avec A2=GND

void setup() {
  Serial.begin(115200);
  if (energy.begin() != ES_OK) {
    Serial.println("Erreur EEPROM");
  }
}

void loop() {
  // Exemple : valeurs qui doivent provenir du PZEM et du DS3231 réels.
  // energy.update(2026, 11, 14, 10, 30, totalWh);
  // Serial.println(energy.todayWh());
}
```

Pour une intégration complète avec le module PZEM réel, voir `examples/MonitoringAvecPZEM/MonitoringAvecPZEM.ino`.

### Interface utile

| Méthode | Utilité |
|---|---|
| `begin()` | Lecture de la mémoire et récupération du dernier point valide |
| `update(année, mois, jour, heure, minute, totalWh)` | Acquisition énergétique et sauvegarde périodique |
| `todayWh()` | Énergie consommée pendant la période suivie du jour |
| `monthWh()` | Énergie consommée depuis le début de la période mensuelle suivie |
| `totalWh()` | Dernier compteur cumulé lu |
| `todayPartial()` | Vrai si le début de journée ou la continuité sont incomplets |
| `getHistory(records, 7)` | Jours clôturés de la fenêtre de 7 jours, du plus ancien au plus récent |
| `decodeDayKey(key, y, m, d)` | Convertit un index de jour en date civile |

Les appels à `update` doivent se faire avec une date fiable et des valeurs issues d'une **lecture PZEM réussie**. La bibliothèque suppose que l'énergie cumulée PZEM est monotone et conservée pendant les coupures. Elle renvoie `ES_COUNTER_RESET` si cette hypothèse est violée et **n'invente pas** de consommation.

## Synchronisation Cloud prévue (à réaliser dans le firmware ESP32)

- Afficher en direct toutes les grandeurs mesurées par le PZEM (ex. toutes les 1–5 secondes), **sans les conserver dans l'historique**.
- Envoyer l'énergie cumulée en temps réel et, en différé ou après retour de connexion, les 7 enregistrements journaliers de `getHistory()`.
- Côté serveur, utiliser la **clé unique `(device_id, date)`** pour accepter plusieurs envois du même jour sans duplication (`UPSERT`). Préserver le statut de qualité du jour (`COMPLETE` / `PARTIAL`).
- Calculer et archiver les consommations **mensuelles** dans le Cloud à partir des journées disponibles. En cas de journées manquantes, signaler un mois incomplet.
- Le firmware peut renvoyer les sept relevés à chaque reconnexion : l'EEPROM ne stocke **aucun accusé de réception**, ce qui évite de les perdre si l'acquittement arrive juste au moment d'une coupure.

Exemple de message **illustratif**, non émis par cette bibliothèque :

```json
{
  "device_id": "compteur-001",
  "date": "2026-11-13",
  "energy_wh": 4500,
  "quality": "COMPLETE"
}
```

## Limites importantes

1. **Après sept jours hors connexion**, les relevés les plus anciens sont remplacés même s'ils n'ont pas atteint le Cloud. Cela correspond à la limite demandée : un réseau indisponible plus d'une semaine peut donc provoquer une perte irréversible d'historique quotidien.
2. Si l'ATmega328P ne fonctionne pas pendant qu'une consommation continue, le PZEM peut connaître l'énergie cumulée au retour, mais **ne permet pas de répartir rétroactivement cette énergie sur chaque jour**. Le relevé est alors partiel et des journées peuvent manquer.
3. Les démarrages en cours de journée ou de mois donnent des totaux **partiels** jusqu'au prochain début de période. Un RTC mal réglé ne doit pas être accepté.
4. Une erreur CRC peut rendre un dernier enregistrement indisponible ; la rotation évite qu'une écriture incomplète détruise systématiquement la sauvegarde précédente, mais **l'électronique et l'alimentation doivent être correctement conçues**.
5. Si le PZEM est remis à zéro ou remplacé, la continuité énergétique n'est plus automatique : intervention de maintenance et procédure de réassociation sont nécessaires.
6. `PZEM004T_V1 v0.1.0` expose l'énergie via un `float` ; la précision au Wh près diminue lorsque le cumul augmente. Cette première bibliothèque de stockage ne corrige pas la précision de la source.
7. Le système Cloud (Wi-Fi, API, serveur, tableau de bord) et la validation sur matériel restent à réaliser. L'exemple fourni ne fait **aucun appel réseau**.

## Vérifications de développement

Code C++11 validé par `g++` avec **simulation du bus I²C et de l'EEPROM** : conservation des données au redémarrage, fenêtre glissante de 7 jours, changement de mois, horloge en retard, remise à zéro du compteur, puce absente et écriture corrompue.

**Non encore testé** : compilation AVR effective dans Arduino IDE et essais sur 24C08 physique. Aucune évolution de `PZEM004T_V1` n'a été réalisée.

## Sécurité

Le PZEM-004T mesure des circuits raccordés au secteur. Toute intervention sur la partie 230 V doit être faite hors tension par une personne qualifiée, dans un boîtier adapté avec isolation électrique appropriée.

## Licence

MIT — Copyright (c) 2026 sandjongalex.

# Herkunft und Drittkomponenten (Qualia-Uhr)

## Zifferblatt
Das Zifferblatt der Uhr orientiert sich an einer Bahnhofsuhr-Vorlage von **ludi.de**, die unter der
GNU General Public License (GPL) steht. Dieses Projekt steht deshalb ebenfalls unter der GPL, Version 3
(siehe `../LICENSE`). Die Vorlage wurde für dieses Projekt verändert (Umsetzung als LVGL-Zeichnung,
Sekundenzeiger wahlweise als Lochzeiger oder einfache rote Linie, ohne Firmenlogo).

> https://www.ludi.de

## Bibliotheken (jeweils unter eigener Lizenz, bitte dort nachlesen)
- LVGL 9.3.0 (MIT)
- GFX Library for Arduino von moononournation (Treiber für das RGB-Panel)
- RTClib von Adafruit (MIT)
- esp32-Board-Paket von Espressif (Arduino-ESP32)

Die Bibliotheken sind nicht Teil dieses Repositorys; sie werden über den Arduino-Bibliotheksverwalter installiert.

## Zeitquellen
NTP-Server aus dem Pool `pool.ntp.org` (Einstellung in `konfiguration.h`); DCF77 ist das Zeitsignal der
Physikalisch-Technischen Bundesanstalt (Sender Mainflingen).

## Marken
"Adafruit", "Qualia" und weitere Namen sind Marken ihrer jeweiligen Inhaber und werden hier nur zur
Beschreibung der Kompatibilität genannt. Dieses Projekt steht in keiner Verbindung zu diesen Firmen.

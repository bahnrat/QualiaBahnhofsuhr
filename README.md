# Bahnhofsuhr für das Adafruit Qualia ESP32-S3 (TL040WVS03, 480×480)

*In short (English):* A stand-alone station clock for the Adafruit Qualia ESP32-S3 with the 4" 480×480
TL040WVS03 display. Time comes from a DS3231 RTC, set via Wi-Fi (NTP) and/or an optional DCF77 receiver
module. Clock face only: no touch menu, no TOTP. Documentation is in German. Licence: GPL-3.0.

Nur die Uhr: ein quadratisches (oder rundes) Bahnhofs-Zifferblatt mit sanft springendem Sekundenzeiger.
Gedacht für ein Gehäuse, in dem die Uhr tagsüber läuft, zum Beispiel bei einer Modellbahn. Es gibt kein Menü,
keine PIN und kein TOTP; das alles steckt in dem Waveshare-Sketch im Nachbarordner.

## Was es kann

- **Bahnhofsuhr** mit Lochzeiger (rote Scheibe mit Loch) oder einfacher roter Linie, auf quadratischem
  weißem oder rundem Zifferblatt
- **Schweizer Kellenzeiger** und Telenorma-Logo sind aus lizenzrechtlichen Gründen nicht enthalten.
- **Echtzeituhr DS3231** (Adafruit, per STEMMA QT/Qwiic): hält die Zeit auch ohne Strom
- **Zeit per WLAN/NTP** beim Start (bis zu drei Netze, hat Vorrang), danach wird WLAN wieder abgeschaltet
- **Zeit per DCF77** mit einem Empfangsmodul (optional): stellt die DS3231 nach, wenn kein WLAN da ist
  (zum Beispiel unterwegs). Erkennt den Impulspegel selbst, ein roter Punkt oben links blinkt im Takt der
  Sekundenimpulse und hilft beim Ausrichten der Antenne
- **Stromsparen:** CPU-Takt und Pause der Hauptschleife einstellbar (`CPU_TAKT_MHZ`, `SCHLEIFE_PAUSE_MS`
  in der `konfiguration.h`)

## Hardware

| Teil | Bemerkung |
|---|---|
| Adafruit Qualia ESP32-S3 für RGB-666-Displays | mit OPI-PSRAM |
| Display TL040WVS03 (4", 480×480) | die Touch-Funktion wird von der Uhr nicht benutzt |
| Adafruit DS3231 Präzisions-RTC | über STEMMA QT an die Qualia, Knopfzelle einlegen |
| DCF77-Empfänger (optional) | Plus an **3,3 V** (nicht 5 V), Masse an GND, Ausgang an `A0` (GPIO 17); Pin in `konfiguration.h` änderbar |

## Schnellstart

1. **Arduino IDE** mit dem Board-Paket *esp32 (Espressif) 3.3.x*.
2. Bibliotheken (Bibliotheksverwalter): **lvgl 9.3.0**, **GFX Library for Arduino 1.6.9**, **RTClib**.
3. **lvgl einrichten:** `lv_conf.h` gehört in den `libraries`-Ordner (nicht in den Sketch-Ordner). Wichtig:
   `LV_COLOR_DEPTH 16` und die Schrift `LV_FONT_MONTSERRAT_20 = 1`.
4. Den Ordner `bahnhofsuhr_qualia_uhr/` öffnen. Einstellungen:
   - **Board:** „Adafruit Qualia ESP32-S3 RGB666“, **PSRAM:** „OPI PSRAM“
5. **`konfiguration.h`** anpassen: WLAN (leer lassen = kein WLAN), Zeitzone, DCF77 an/aus, Zifferblatt,
   Sekundenzeiger.
6. Hochladen. Ohne gültige Zeit zeigt die Uhr einen Hinweis; sobald WLAN oder DCF77 die Zeit gestellt haben,
   läuft sie.

Im Seriellen Monitor (115200 Baud) stehen Meldungen wie „RTC per NTP gestellt.“ oder „RTC per DCF77 gestellt.“.
DCF77 braucht für eine gültige Zeit mindestens zwei aufeinanderfolgende Minuten mit gutem Empfang; das kann
einige Minuten dauern.

## Wichtige Hinweise

- **Zugangsdaten:** Das WLAN-Passwort liegt **unverschlüsselt im Flash**. Eine eigene, ausgefüllte
  `konfiguration.h` **nie** veröffentlichen.
- **Flackert das Bild,** `DISPLAY_PIXELTAKT` in der `konfiguration.h` auf 12000000 oder 16000000 stellen.
- **DCF77-Antenne** weit weg von Display, Netzteil und anderen Störquellen anbringen (am besten außerhalb
  des Gehäuses). Ferritantennen müssen quer zur Richtung Frankfurt stehen.
- **Zeitzone** und Sommer-/Winterzeit stehen in `TZ_STRING` (Standard: Deutschland).

## Lizenz und Herkunft

GPL-3.0, siehe `../LICENSE`. Herkunft des Zifferblatts und Drittkomponenten: [NOTICE.md](NOTICE.md).

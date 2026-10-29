// ============================================================
//  konfiguration.h  -  Persoenliche Einstellungen (Qualia-Uhr)
//  Diese Datei liegt im selben Ordner wie bahnhofsuhr_qualia_uhr.ino.
//  Nur HIER etwas aendern, das Hauptprogramm bleibt unberuehrt.
//  Anfuehrungszeichen und Semikolons nicht loeschen!
// ============================================================
#pragma once

// ---------- WLAN (nur fuer den Zeitabgleich) ----------
// Bis zu drei Netze. Leere Anfuehrungszeichen "" = Eintrag unbenutzt.
// Hinweis: Die Zugangsdaten liegen im Flash des Geraets (nicht verschluesselt).
const char *WLAN_SSIDS[3]       = {"", "", ""};
const char *WLAN_PASSWOERTER[3] = {"", "", ""};

// true  = beim Start zuerst per WLAN die Zeit holen (hat Vorrang vor DCF77).
//         Ist kein bekanntes WLAN da, dauert das bis zu 10 s, dann laeuft DCF77.
//         Bleibt die Zeit ungueltig, wird alle paar Minuten erneut versucht.
// false = kein WLAN
// Ohne eingetragenes Netz (leere Anfuehrungszeichen oben) wird WLAN nie versucht.
const bool ZEIT_BEIM_START_PER_WLAN = true;

// Zeitserver und Wartezeiten (normalerweise nicht aendern)
const char *NTP_SERVER_1 = "de.pool.ntp.org";
const char *NTP_SERVER_2 = "pool.ntp.org";
const unsigned long WLAN_TIMEOUT_MS = 10000;
const unsigned long NTP_TIMEOUT_MS = 8000;

// Zeitzone (Standard: Deutschland, Sommer-/Winterzeit automatisch)
const char *TZ_STRING = "CET-1CEST,M3.5.0,M10.5.0/3";

// ---------- DCF77-Funkuhr-Empfaenger ----------
// true  = DCF77-Modul angeschlossen: stellt die Echtzeituhr (DS3231) nach
// false = kein DCF77-Modul
const bool DCF_EINBAUEN = true;

// Anschluss des Modul-Ausgangs. A0 (= GPIO 17) und A1 (= GPIO 16) sind am Qualia frei.
// Modul: Plus an 3,3 V (NICHT an 5 V!), Masse an GND, Ausgang an diesen Pin.
const int DCF_PIN = A0;

// Interner Pull-up-Widerstand am Pin. Bei Modulen mit Open-Collector-Ausgang noetig,
// bei Modulen mit fertigem Ausgang (3,3 V) schadet er nicht.
const bool DCF_PULLUP = true;

// Welcher Pegel bedeutet "Sekundenimpuls"?
// 0 = automatisch erkennen (empfohlen)
// 1 = HIGH-Impuls, 2 = LOW-Impuls (nur falls die Erkennung nicht klappt)
const int DCF_POLARITAET = 0;

// Nach erfolgreichem Stellen erst nach so vielen Stunden wieder abgleichen
const int DCF_ABGLEICH_ALLE_STUNDEN = 24;

// Kleiner roter Punkt oben links, der im Rhythmus der Sekundenimpulse blinkt.
// Hilfreich zum Ausrichten der Antenne; danach auf false stellen.
const bool DCF_ANZEIGE_PUNKT = true;

// ---------- Uhr: Form des Zifferblatts ----------
// true  = Zifferblatt quadratisch (das ganze Display ist weiss, die Uhr bleibt rund)
// false = rundes Zifferblatt auf schwarzem Grund
const bool UHR_ZIFFERBLATT_QUADRATISCH = true;
// Nur beim quadratischen Zifferblatt: true = duenner schwarzer Ring um die Uhr, false = ohne Ring
const bool UHR_QUADRAT_RING = false;

// ---------- Uhr: Form des Sekundenzeigers ----------
// true  = Lochzeiger (rote Scheibe mit Loch, deutsche Bahnhofsuhr)
// false = einfache rote Linie
const bool UHR_SEKUNDENZEIGER_ALS_LOCHZEIGER = true;

// ---------- Display ----------
// Pixeltakt in Hz; 0 = Standard der Bibliothek. Bei Flackern 12000000 oder 16000000 probieren.
const uint32_t DISPLAY_PIXELTAKT = 0;

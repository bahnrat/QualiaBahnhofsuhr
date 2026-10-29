// ============================================================
//  Bahnhofsuhr fuer das Adafruit Qualia ESP32-S3 mit TL040WVS03 (480x480)
//  Nur die Uhr: kein PIN, kein TOTP, kein Menue.
//
//  Arduino IDE:
//    Board:      "Adafruit Qualia ESP32-S3 RGB666"
//    PSRAM:      "OPI PSRAM"
//    DCF77-Modul (optional) an A0, siehe konfiguration.h
//    Bibliotheken: lvgl 9.3.0, GFX Library for Arduino 1.6.9, RTClib
//    lv_conf.h (im Ordner libraries): LV_FONT_MONTSERRAT_20 = 1
//
//  Einstellungen stehen in konfiguration.h.
// ============================================================
#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <esp_sntp.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <time.h>
#include <math.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include "konfiguration.h"

// Stromsparen: Vorgaben, falls in konfiguration.h nichts steht (dann aendert sich nichts gegenueber vorher)
#ifndef CPU_TAKT_MHZ
#define CPU_TAKT_MHZ 240        // 80, 160 oder 240
#endif
#ifndef SCHLEIFE_PAUSE_MS
#define SCHLEIFE_PAUSE_MS 5     // Pause pro Durchlauf der Hauptschleife (1 bis 20)
#endif

// ---------- Display (Aufbau wie in Adafruits Demo; Pin-Namen kommen aus dem Board-Profil) ----------
const int DISPLAY_BREITE = 480;
const int DISPLAY_HOEHE = 480;

Arduino_XCA9554SWSPI *expander = new Arduino_XCA9554SWSPI(
    PCA_TFT_RESET, PCA_TFT_CS, PCA_TFT_SCK, PCA_TFT_MOSI, &Wire, 0x3F);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    TFT_DE, TFT_VSYNC, TFT_HSYNC, TFT_PCLK,
    TFT_R1, TFT_R2, TFT_R3, TFT_R4, TFT_R5,
    TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
    TFT_B1, TFT_B2, TFT_B3, TFT_B4, TFT_B5,
    1 /* hsync_polarity */, 46 /* hsync_front_porch */, 2 /* hsync_pulse_width */, 44 /* hsync_back_porch */,
    1 /* vsync_polarity */, 50 /* vsync_front_porch */, 16 /* vsync_pulse_width */, 16 /* vsync_back_porch */);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    DISPLAY_BREITE, DISPLAY_HOEHE, rgbpanel, 0 /* Rotation */, true /* auto_flush */,
    expander, GFX_NOT_DEFINED /* RST */, tl040wvs03_init_operations, sizeof(tl040wvs03_init_operations));

// ---------- Echtzeituhr (haelt UTC) ----------
RTC_DS3231 rtc;
bool rtcVorhanden = false;
bool rtcZeitGueltig = false;
const time_t ZEIT_PLAUSIBEL_AB = 1735689600; // 01.01.2025 - alles davor ist garantiert falsch
WiFiMulti wifiMulti;
volatile bool ntpSyncFertig = false;
bool wlanZeitGestellt = false;      // Zeit wurde per WLAN geholt (hat Vorrang vor DCF77)
unsigned long wlanZeitMillis = 0;

// ---------- LVGL ----------
lv_display_t *anzeige;
lv_color_t *zeichenpuffer;
lv_obj_t *uhr_screen = NULL;
lv_obj_t *hinweis_screen = NULL;
lv_obj_t *hinweis_label = NULL;
bool uhrLaeuft = false;
lv_obj_t *dcf_punkt = NULL;

static uint32_t millisCallback() { return millis(); }

static void anzeigeFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
    lv_display_flush_ready(disp);
}

static void rundenCallback(lv_event_t *e) {
    lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
    a->x1 = (a->x1 >> 1) << 1;
    a->y1 = (a->y1 >> 1) << 1;
    a->x2 = ((a->x2 >> 1) << 1) + 1;
    a->y2 = ((a->y2 >> 1) << 1) + 1;
}

static void dekoObjekt(lv_obj_t *o) {
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_outline_width(o, 0, 0);
}

// ============================================================
//  Zeit: RTC und Abgleich per WLAN
// ============================================================
static bool wlanKonfiguriert() {
    if (!ZEIT_BEIM_START_PER_WLAN) return false;
    for (int i = 0; i < 3; i++) {
        if (strlen(WLAN_SSIDS[i]) > 0) return true;
    }
    return false;
}

static void ntpSyncCallback(struct timeval *tv) { ntpSyncFertig = true; }

static void rtcPruefen() {
    rtcZeitGueltig = rtcVorhanden && !rtc.lostPower() && (time_t)rtc.now().unixtime() >= ZEIT_PLAUSIBEL_AB;
}

// Verbindet kurz per WLAN, holt die Zeit per NTP, stellt die RTC und schaltet den Funk
// danach wieder KOMPLETT ab. Blockiert bis zu ca. 20 Sekunden.
static bool zeitPerWlanAbgleichen() {
    if (!rtcVorhanden || !wlanKonfiguriert()) return false;

    static bool initialisiert = false;
    if (!initialisiert) {
        for (int i = 0; i < 3; i++) {
            if (strlen(WLAN_SSIDS[i]) > 0) wifiMulti.addAP(WLAN_SSIDS[i], WLAN_PASSWOERTER[i]);
        }
        sntp_set_time_sync_notification_cb(ntpSyncCallback);
        initialisiert = true;
    }

    bool erfolg = false;
    WiFi.mode(WIFI_STA);
    unsigned long start = millis();
    while (wifiMulti.run() != WL_CONNECTED && millis() - start < WLAN_TIMEOUT_MS) delay(200);

    if (WiFi.status() == WL_CONNECTED) {
        ntpSyncFertig = false;
        configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2); // UTC, keine Offsets
        start = millis();
        while (!ntpSyncFertig && millis() - start < NTP_TIMEOUT_MS) delay(100);
        time_t jetzt = time(nullptr);
        if (ntpSyncFertig && jetzt >= ZEIT_PLAUSIBEL_AB) {
            rtc.adjust(DateTime((uint32_t)jetzt)); // loescht auch das Flag "Strom war weg"
            rtcPruefen();
            erfolg = rtcZeitGueltig;
            if (erfolg) { wlanZeitGestellt = true; wlanZeitMillis = millis(); }
            Serial.println("RTC per NTP gestellt.");
        } else {
            Serial.println("NTP: keine plausible Zeit erhalten.");
        }
    } else {
        Serial.println("Zeitabgleich: kein bekanntes WLAN erreichbar.");
    }

    // Funk immer wieder komplett abschalten - auch bei Misserfolg.
    esp_sntp_stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return erfolg;
}

// ============================================================
//  Hinweisbildschirm (Zeit wird geholt / Fehler)
// ============================================================
static void zeigeHinweis(const char *text) {
    if (!hinweis_screen) {
        hinweis_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(hinweis_screen, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(hinweis_screen, LV_OPA_COVER, 0);
        hinweis_label = lv_label_create(hinweis_screen);
        lv_obj_set_width(hinweis_label, 400);
        lv_obj_set_style_text_font(hinweis_label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(hinweis_label, lv_color_white(), 0);
        lv_obj_set_style_text_align(hinweis_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(hinweis_label);
    }
    lv_label_set_text(hinweis_label, text);
    lv_screen_load(hinweis_screen);
    lv_refr_now(NULL); // sofort zeichnen, auch wenn danach blockiert wird
}


// ============================================================
//  DCF77-Empfang
//  Der Interrupt merkt sich nur Zeit und Pegel jeder Flanke. Die Auswertung
//  (Entprellen, Bits, Minutentelegramm) laeuft in loop().
// ============================================================
static const int DCF_RING = 128;
static volatile int64_t dcfRingT[DCF_RING];
static volatile uint8_t dcfRingP[DCF_RING];
static volatile uint16_t dcfKopf = 0;
static uint16_t dcfSchwanz = 0;

static bool dcfBekannt = false;          // mindestens eine Flanke gesehen
static uint8_t dcfPegel = 0;             // Pegel nach der letzten Flanke
static int64_t dcfKanteT = 0;
static int dcfPulsPegel = (DCF_POLARITAET == 1) ? 1 : (DCF_POLARITAET == 2 ? 0 : -1);
static int dcfStimmen[2] = {0, 0};
static bool dcfImPuls = false;
static int64_t dcfPulsStart = 0;
static int64_t dcfLetzterStart = 0;
static int dcfNaechster = -1;            // Nummer des Bits (Sekunde), das gerade empfangen wird; -1 = noch nicht synchron
static uint8_t dcfBits[60];
static time_t dcfVorher = 0;             // UTC der zuletzt gueltig empfangenen Minute
static bool dcfGestellt = false;
static unsigned long dcfLetzteStellung = 0;
static bool dcfNeuGestellt = false;

static void IRAM_ATTR dcfIsr() {
    int64_t t = esp_timer_get_time();
    uint8_t p = (uint8_t)gpio_get_level((gpio_num_t)DCF_PIN);
    uint16_t k = dcfKopf;
    dcfRingT[k] = t;
    dcfRingP[k] = p;
    dcfKopf = (uint16_t)((k + 1) % DCF_RING);
}

static void dcfPunktSetzen(bool an) {
    if (dcf_punkt) lv_obj_set_style_bg_opa(dcf_punkt, an ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

// Tage seit 01.01.1970 (Datum im gregorianischen Kalender)
static int64_t tageSeit1970(int y, int m, int d) {
    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static bool dcfParitaetOk(int von, int bis) {
    int s = 0;
    for (int i = von; i <= bis; i++) s += dcfBits[i];
    return (s & 1) == 0;
}

static int dcfBcd(int von, int n) {
    static const int w[8] = {1, 2, 4, 8, 10, 20, 40, 80};
    int v = 0;
    for (int i = 0; i < n; i++) if (dcfBits[von + i]) v += w[i];
    return v;
}

// Wird im Moment des Minutenanfangs aufgerufen (Beginn des Impulses nach der Luecke).
// Das Telegramm der vergangenen Minute enthaelt genau die Zeit, die jetzt beginnt.
static void dcfMinuteAuswerten() {
    if (dcfBits[0] != 0 || dcfBits[20] != 1) return;
    if (dcfBits[17] == dcfBits[18]) return;                       // genau eines von Z1/Z2
    if (!dcfParitaetOk(21, 28) || !dcfParitaetOk(29, 35) || !dcfParitaetOk(36, 58)) return;
    int minute = dcfBcd(21, 7), stunde = dcfBcd(29, 6);
    int tag = dcfBcd(36, 6), monat = dcfBcd(45, 5), jahr = dcfBcd(50, 8);
    if (minute > 59 || stunde > 23 || tag < 1 || tag > 31 || monat < 1 || monat > 12 || jahr > 99) return;

    int offset = dcfBits[17] ? 7200 : 3600;                       // Z1 = Sommerzeit (UTC+2), Z2 = Winterzeit (UTC+1)
    int64_t lokal = tageSeit1970(2000 + jahr, monat, tag) * 86400LL + stunde * 3600 + minute * 60;
    time_t utc = (time_t)(lokal - offset);
    if (utc < ZEIT_PLAUSIBEL_AB) return;

    bool bestaetigt = (dcfVorher != 0 && utc == dcfVorher + 60);  // zwei Minuten hintereinander stimmig
    dcfVorher = utc;
    Serial.printf("DCF77: Minute gueltig%s\n", bestaetigt ? " (bestaetigt)" : "");

    const unsigned long frist = (unsigned long)DCF_ABGLEICH_ALLE_STUNDEN * 3600000UL;
    bool faellig = !dcfGestellt || (millis() - dcfLetzteStellung) > frist;
    if (wlanZeitGestellt && (millis() - wlanZeitMillis) < frist) faellig = false; // WLAN-Zeit hat Vorrang
    if (bestaetigt && faellig && rtcVorhanden) {
        rtc.adjust(DateTime((uint32_t)utc));
        dcfGestellt = true;
        dcfLetzteStellung = millis();
        rtcPruefen();
        dcfNeuGestellt = true;
        Serial.println("RTC per DCF77 gestellt.");
    }
}

static void dcfKante(int64_t t, uint8_t pegel) {
    if (dcfBekannt) {
        // Das gerade beendete Segment hatte den Pegel !pegel
        int dauer = (int)((t - dcfKanteT) / 1000);
        if (dcfPulsPegel < 0 && dauer >= 70 && dauer <= 260) {
            dcfStimmen[!pegel]++;
            int a = !pegel;
            if (dcfStimmen[a] >= 6 && dcfStimmen[a] >= 3 * dcfStimmen[!a]) {
                dcfPulsPegel = a;
                Serial.printf("DCF77: Impulspegel erkannt: %s\n", a ? "HIGH" : "LOW");
            }
        }
    }
    dcfPunktSetzen(dcfPulsPegel >= 0 ? (pegel == dcfPulsPegel) : (pegel == 1));

    if (dcfPulsPegel >= 0 && dcfBekannt) {
        if (pegel == dcfPulsPegel) {
            // Impulsanfang = Sekundenanfang
            int gap = dcfLetzterStart ? (int)((t - dcfLetzterStart) / 1000) : -1;
            dcfLetzterStart = t;
            if (gap >= 1850 && gap <= 2150) {                       // Minutenluecke
                if (dcfNaechster == 58) dcfMinuteAuswerten();
                dcfNaechster = 0;
            } else if (gap >= 850 && gap <= 1150) {
                if (dcfNaechster >= 0) {
                    dcfNaechster++;
                    if (dcfNaechster > 58) dcfNaechster = -1;
                }
            } else {
                dcfNaechster = -1;
            }
            dcfImPuls = true;
            dcfPulsStart = t;
        } else if (dcfImPuls) {
            // Impulsende: Laenge 100 ms = 0, 200 ms = 1
            dcfImPuls = false;
            int len = (int)((t - dcfPulsStart) / 1000);
            if (dcfNaechster >= 0) {
                if (len >= 50 && len < 150) dcfBits[dcfNaechster] = 0;
                else if (len >= 150 && len < 260) dcfBits[dcfNaechster] = 1;
                else dcfNaechster = -1;
            }
        }
    }
    dcfKanteT = t;
    dcfPegel = pegel;
    dcfBekannt = true;
}

static void dcfVerarbeiten() {
    int64_t jetzt = esp_timer_get_time();
    while (dcfSchwanz != dcfKopf) {
        uint16_t n = (uint16_t)((dcfSchwanz + 1) % DCF_RING);
        int64_t t = dcfRingT[dcfSchwanz];
        if (n != dcfKopf) {
            if (dcfRingT[n] - t < 20000) {               // Stoerimpuls unter 20 ms: beide Flanken verwerfen
                dcfSchwanz = (uint16_t)((n + 1) % DCF_RING);
                continue;
            }
        } else if (jetzt - t < 20000) {
            break;                                        // noch abwarten, ob gleich eine Gegenflanke kommt
        }
        uint8_t pegel = dcfBekannt ? (uint8_t)!dcfPegel : dcfRingP[dcfSchwanz];
        dcfSchwanz = n;
        dcfKante(t, pegel);
    }
}

static void dcfStarten() {
    pinMode(DCF_PIN, DCF_PULLUP ? INPUT_PULLUP : INPUT);
    attachInterrupt(digitalPinToInterrupt(DCF_PIN), dcfIsr, CHANGE);
    Serial.printf("DCF77-Empfang auf GPIO %d gestartet.\n", (int)DCF_PIN);
}

// Anzeige waehrend des Wartens auf die erste gueltige Zeit (zum Ausrichten der Antenne)
static void dcfHinweisAktualisieren() {
    char text[120];
    if (dcfPulsPegel < 0)
        snprintf(text, sizeof(text), "Zeit ungueltig.\nWarte auf DCF77 ...\nSuche Sekundenimpulse");
    else if (dcfNaechster < 0)
        snprintf(text, sizeof(text), "Zeit ungueltig.\nWarte auf DCF77 ...\nSuche Minutenanfang");
    else
        snprintf(text, sizeof(text), "Zeit ungueltig.\nWarte auf DCF77 ...\nSekunde %d von 59", dcfNaechster);
    zeigeHinweis(text);
}

// ============================================================
//  Uhr
// ============================================================
const int UHR_MITTE_X = 240;
const int UHR_MITTE_Y = 240;
const float UHR_GESICHT_RADIUS = 215.0f;
const float UHR_SKALIERUNG = UHR_GESICHT_RADIUS / 100.0f;

const float UHR_STUNDE_SPITZE = 60.0f * UHR_SKALIERUNG;
const float UHR_STUNDE_SCHWANZ = 18.0f * UHR_SKALIERUNG; // Verlaengerung hinter der Achse
const float UHR_MINUTE_SPITZE = 94.0f * UHR_SKALIERUNG;
const float UHR_MINUTE_SCHWANZ = 18.0f * UHR_SKALIERUNG; // Verlaengerung hinter der Achse
const float UHR_SEKUNDE_SPITZE = 90.2f * UHR_SKALIERUNG;
const float UHR_SEKUNDE_SCHWANZ = 22.0f * UHR_SKALIERUNG;
const float UHR_SEKUNDE_SCHAFT_UNTEN_ENDE = 42.0f * UHR_SKALIERUNG;
const float UHR_SEKUNDE_SCHAFT_OBEN_START = 58.0f * UHR_SKALIERUNG;
const float UHR_SEKUNDE_RING_ABSTAND = 50.0f * UHR_SKALIERUNG;
const float UHR_SEKUNDE_RING_RADIUS = 8.5f * UHR_SKALIERUNG;
const float UHR_SEKUNDE_RING_DICKE = 6.5f * UHR_SKALIERUNG;

lv_obj_t *uhr_stunden_zeiger, *uhr_minuten_zeiger;
lv_obj_t *uhr_lochzeiger;
lv_obj_t *uhr_sekunden_zeiger;
lv_timer_t *uhr_timer = NULL;

lv_point_precise_t uhr_stunden_punkte[2];
lv_point_precise_t uhr_minuten_punkte[2];
lv_point_precise_t uhr_sekunden_unten_punkte[2];
lv_point_precise_t uhr_sekunden_oben_punkte[2];
lv_point_t uhr_lochzeiger_ring_mitte;
lv_point_precise_t uhr_sekunden_punkte[2];
lv_point_precise_t strich_punkte[60][2];

struct tm uhrLetzteZeit;
time_t uhrLetzteUtc = 0;
unsigned long uhrBasisMillis = 0;
bool uhrBasisInitialisiert = false;

static void punktAufKreis(float radius, float winkelGrad, lv_point_precise_t *ziel) {
    float rad = (winkelGrad - 90.0f) * (float)M_PI / 180.0f;
    ziel->x = (lv_value_precise_t)(UHR_MITTE_X + (int)lroundf(radius * cosf(rad)));
    ziel->y = (lv_value_precise_t)(UHR_MITTE_Y + (int)lroundf(radius * sinf(rad)));
}

static void zeigerZeichnen(lv_obj_t *linie, lv_point_precise_t *punkte, float winkel, float spitze, float schwanz) {
    punktAufKreis(schwanz, winkel + 180.0f, &punkte[0]);
    punktAufKreis(spitze, winkel, &punkte[1]);
    lv_line_set_points(linie, punkte, 2);
}

// Lochzeiger: Schaft unten, Schaft oben und Ring aus EINEM Zeichenvorgang.
static void lochzeigerZeichnen(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_color_t rot = lv_color_hex(0xad3e00);

    lv_draw_line_dsc_t linie;
    lv_draw_line_dsc_init(&linie);
    linie.color = rot;
    linie.width = 4;
    linie.opa = LV_OPA_COVER;
    linie.round_start = 0;
    linie.round_end = 0;

    linie.p1 = uhr_sekunden_unten_punkte[0];
    linie.p2 = uhr_sekunden_unten_punkte[1];
    lv_draw_line(layer, &linie);
    linie.p1 = uhr_sekunden_oben_punkte[0];
    linie.p2 = uhr_sekunden_oben_punkte[1];
    lv_draw_line(layer, &linie);

    lv_draw_arc_dsc_t ring;
    lv_draw_arc_dsc_init(&ring);
    ring.color = rot;
    ring.opa = LV_OPA_COVER;
    ring.center = uhr_lochzeiger_ring_mitte;
    ring.radius = (uint16_t)lroundf(UHR_SEKUNDE_RING_RADIUS + UHR_SEKUNDE_RING_DICKE / 2.0f);
    ring.width = (uint16_t)lroundf(UHR_SEKUNDE_RING_DICKE);
    ring.start_angle = 0;
    ring.end_angle = 360;
    lv_draw_arc(layer, &ring);
}

static void uhrZeigerAktualisieren(const struct tm &t, unsigned long msInnerhalb) {
    float stundenWinkel = (float)(t.tm_hour % 12) * 30.0f + (float)t.tm_min * 0.5f;
    float minutenWinkel = (float)t.tm_min * 6.0f;

    // Sekundenzeiger wie bei der Bahnhofsuhr: 58,5 s Lauf, dann kurzer Halt
    const float stopTime = 1.5f;
    float fact = 60.0f / (60.0f - stopTime);
    float virt = fact * (1000.0f * (float)t.tm_sec + (float)msInnerhalb);
    if (virt > 60000.0f) virt = 60000.0f;
    int virtSek = (int)(virt / 1000.0f);
    float virtMs = fmodf(virt, 1000.0f);
    float sekWinkel = 6.0f * (float)virtSek;
    sekWinkel += 3.0f * (1.0f + cosf((float)M_PI + (float)M_PI * (0.001f * virtMs)));

    zeigerZeichnen(uhr_stunden_zeiger, uhr_stunden_punkte, stundenWinkel, UHR_STUNDE_SPITZE, UHR_STUNDE_SCHWANZ);
    zeigerZeichnen(uhr_minuten_zeiger, uhr_minuten_punkte, minutenWinkel, UHR_MINUTE_SPITZE, UHR_MINUTE_SCHWANZ);

    if (!UHR_SEKUNDENZEIGER_ALS_LOCHZEIGER) {
        zeigerZeichnen(uhr_sekunden_zeiger, uhr_sekunden_punkte, sekWinkel, UHR_SEKUNDE_SPITZE, UHR_SEKUNDE_SCHWANZ);
    } else {
        punktAufKreis(UHR_SEKUNDE_SCHWANZ, sekWinkel + 180.0f, &uhr_sekunden_unten_punkte[0]);
        punktAufKreis(UHR_SEKUNDE_SCHAFT_UNTEN_ENDE, sekWinkel, &uhr_sekunden_unten_punkte[1]);
        punktAufKreis(UHR_SEKUNDE_SCHAFT_OBEN_START, sekWinkel, &uhr_sekunden_oben_punkte[0]);
        punktAufKreis(UHR_SEKUNDE_SPITZE, sekWinkel, &uhr_sekunden_oben_punkte[1]);
        lv_point_precise_t m;
        punktAufKreis(UHR_SEKUNDE_RING_ABSTAND, sekWinkel, &m);
        uhr_lochzeiger_ring_mitte.x = (int32_t)m.x;
        uhr_lochzeiger_ring_mitte.y = (int32_t)m.y;

        // Das Objekt deckt genau den Kasten um den Zeiger ab: nur dieser Bereich wird neu gezeichnet
        int32_t rand = (int32_t)lroundf(UHR_SEKUNDE_RING_RADIUS + UHR_SEKUNDE_RING_DICKE / 2.0f) + 3;
        int32_t x1 = (int32_t)m.x - rand, x2 = (int32_t)m.x + rand;
        int32_t y1 = (int32_t)m.y - rand, y2 = (int32_t)m.y + rand;
        for (int k = 0; k < 2; k++) {
            int32_t ux = (int32_t)uhr_sekunden_unten_punkte[k].x, uy = (int32_t)uhr_sekunden_unten_punkte[k].y;
            int32_t ox = (int32_t)uhr_sekunden_oben_punkte[k].x, oy = (int32_t)uhr_sekunden_oben_punkte[k].y;
            if (ux - 3 < x1) x1 = ux - 3; if (ux + 3 > x2) x2 = ux + 3;
            if (uy - 3 < y1) y1 = uy - 3; if (uy + 3 > y2) y2 = uy + 3;
            if (ox - 3 < x1) x1 = ox - 3; if (ox + 3 > x2) x2 = ox + 3;
            if (oy - 3 < y1) y1 = oy - 3; if (oy + 3 > y2) y2 = oy + 3;
        }
        lv_obj_set_pos(uhr_lochzeiger, x1, y1);
        lv_obj_set_size(uhr_lochzeiger, x2 - x1 + 1, y2 - y1 + 1);
    }
}

static void uhrTimerCallback(lv_timer_t *timer) {
    time_t utc = (time_t)rtc.now().unixtime();
    if (!uhrBasisInitialisiert || utc != uhrLetzteUtc) {
        uhrLetzteUtc = utc;
        localtime_r(&utc, &uhrLetzteZeit);
        uhrBasisMillis = millis();
        uhrBasisInitialisiert = true;
    }
    unsigned long ms = millis() - uhrBasisMillis;
    if (ms > 999) ms = 999;
    uhrZeigerAktualisieren(uhrLetzteZeit, ms);
}

static void erstelleUhrScreen() {
    uhr_screen = lv_obj_create(NULL);
    dekoObjekt(uhr_screen);
    // Quadratisches Zifferblatt: das ganze Display ist weiss, die Uhr bleibt rund
    lv_obj_set_style_bg_color(uhr_screen, UHR_ZIFFERBLATT_QUADRATISCH ? lv_color_white() : lv_color_black(), 0);
    lv_obj_set_style_bg_opa(uhr_screen, LV_OPA_COVER, 0);

    lv_obj_t *dial = lv_obj_create(uhr_screen);
    dekoObjekt(dial);
    lv_obj_set_size(dial, 460, 460);
    lv_obj_center(dial);
    lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dial, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(dial, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dial, (!UHR_ZIFFERBLATT_QUADRATISCH || UHR_QUADRAT_RING) ? 4 : 0, 0);
    lv_obj_set_style_border_color(dial, lv_color_black(), 0);

    for (int i = 0; i < 60; i++) {
        float winkel = i * 6.0f;
        float innen;
        int breite;
        if (i % 15 == 0) {
            innen = UHR_GESICHT_RADIUS - 30.0f * UHR_SKALIERUNG;
            breite = 18;
        } else if (i % 5 == 0) {
            innen = UHR_GESICHT_RADIUS - 24.0f * UHR_SKALIERUNG;
            breite = 18;
        } else {
            innen = UHR_GESICHT_RADIUS - 8.0f * UHR_SKALIERUNG;
            breite = 8;
        }
        punktAufKreis(UHR_GESICHT_RADIUS, winkel, &strich_punkte[i][0]);
        punktAufKreis(innen, winkel, &strich_punkte[i][1]);
        lv_obj_t *strich = lv_line_create(uhr_screen);
        lv_obj_remove_flag(strich, LV_OBJ_FLAG_CLICKABLE);
        lv_line_set_points(strich, strich_punkte[i], 2);
        lv_obj_set_style_line_width(strich, breite, 0);
        lv_obj_set_style_line_color(strich, lv_color_hex(0x444444), 0);
    }

    uhr_stunden_zeiger = lv_line_create(uhr_screen);
    lv_obj_remove_flag(uhr_stunden_zeiger, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_line_width(uhr_stunden_zeiger, 22, 0);
    lv_obj_set_style_line_color(uhr_stunden_zeiger, lv_color_hex(0x212121), 0);

    uhr_minuten_zeiger = lv_line_create(uhr_screen);
    lv_obj_remove_flag(uhr_minuten_zeiger, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_line_width(uhr_minuten_zeiger, 18, 0);
    lv_obj_set_style_line_color(uhr_minuten_zeiger, lv_color_hex(0x232323), 0);

    const lv_color_t rot = lv_color_hex(0xad3e00);
    if (UHR_SEKUNDENZEIGER_ALS_LOCHZEIGER) {
        uhr_lochzeiger = lv_obj_create(uhr_screen);
        dekoObjekt(uhr_lochzeiger);
        lv_obj_set_style_bg_opa(uhr_lochzeiger, LV_OPA_TRANSP, 0);
        lv_obj_set_size(uhr_lochzeiger, 10, 10);
        lv_obj_add_event_cb(uhr_lochzeiger, lochzeigerZeichnen, LV_EVENT_DRAW_MAIN, NULL);
    } else {
        uhr_sekunden_zeiger = lv_line_create(uhr_screen);
        lv_obj_remove_flag(uhr_sekunden_zeiger, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_line_width(uhr_sekunden_zeiger, 4, 0);
        lv_obj_set_style_line_color(uhr_sekunden_zeiger, rot, 0);
    }

    int a = (int)(2.0f * 7.0f * UHR_SKALIERUNG);
    lv_obj_t *mitte = lv_obj_create(uhr_screen);
    dekoObjekt(mitte);
    lv_obj_set_size(mitte, a, a);
    lv_obj_center(mitte);
    lv_obj_set_style_radius(mitte, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(mitte, rot, 0);
    lv_obj_set_style_bg_opa(mitte, LV_OPA_COVER, 0);

    if (DCF_EINBAUEN && DCF_ANZEIGE_PUNKT) {
        dcf_punkt = lv_obj_create(uhr_screen);
        dekoObjekt(dcf_punkt);
        lv_obj_set_size(dcf_punkt, 14, 14);
        lv_obj_set_pos(dcf_punkt, 8, 8);
        lv_obj_set_style_radius(dcf_punkt, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dcf_punkt, rot, 0);
        lv_obj_set_style_bg_opa(dcf_punkt, LV_OPA_TRANSP, 0);
    }

    uhr_timer = lv_timer_create(uhrTimerCallback, 40, NULL); // 25 Hz
    lv_timer_pause(uhr_timer);
}

static void starteUhr() {
    if (!uhr_screen) erstelleUhrScreen();
    uhrBasisInitialisiert = false;
    lv_screen_load(uhr_screen);
    lv_timer_resume(uhr_timer);
    uhrLaeuft = true;
    Serial.println("Uhr gestartet.");
}

// ============================================================
//  Start
// ============================================================
void setup() {
#if CPU_TAKT_MHZ != 240
    setCpuFrequencyMhz(CPU_TAKT_MHZ);   // vor Display und Serial setzen
#endif
    Serial.begin(115200);
    Wire.begin();
    Wire.setClock(400000);

    // Die RTC haelt UTC, die Anzeige rechnet ueber die Zeitzone um.
    setenv("TZ", TZ_STRING, 1);
    tzset();

    rtcVorhanden = rtc.begin();
    if (!rtcVorhanden) Serial.println("Keine RTC gefunden! Kabel pruefen.");
    rtcPruefen();
    if (!rtcZeitGueltig) Serial.println("RTC-Zeit ungueltig (Knopfzelle leer/neu?).");

    bool ok = (DISPLAY_PIXELTAKT > 0) ? gfx->begin(DISPLAY_PIXELTAKT) : gfx->begin();
    if (!ok) Serial.println("FEHLER: gfx->begin() fehlgeschlagen - ist PSRAM (OPI) aktiv?");
    gfx->fillScreen(0x0000);
    expander->pinMode(PCA_TFT_BACKLIGHT, OUTPUT);
    expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);

    lv_init();
    lv_tick_set_cb(millisCallback);

    // Zeichenpuffer: ein ganzes Bild im PSRAM, sonst kleiner Streifenpuffer im RAM
    uint32_t pufferZeilen = DISPLAY_HOEHE;
    uint32_t pufferBytes = DISPLAY_BREITE * pufferZeilen * 2;
    zeichenpuffer = (lv_color_t *)heap_caps_malloc(pufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!zeichenpuffer) {
        pufferZeilen = 40;
        pufferBytes = DISPLAY_BREITE * pufferZeilen * 2;
        zeichenpuffer = (lv_color_t *)heap_caps_malloc(pufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!zeichenpuffer) {
        Serial.println("FEHLER: kein Zeichenpuffer.");
        return;
    }
    Serial.printf("Zeichenpuffer: %u Zeilen\n", (unsigned)pufferZeilen);
    anzeige = lv_display_create(DISPLAY_BREITE, DISPLAY_HOEHE);
    lv_display_set_flush_cb(anzeige, anzeigeFlush);
    lv_display_set_buffers(anzeige, zeichenpuffer, NULL, pufferBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(anzeige, rundenCallback, LV_EVENT_INVALIDATE_AREA, NULL);

    if (wlanKonfiguriert() && rtcVorhanden) {
        zeigeHinweis("Zeit wird per WLAN abgeglichen...");
        zeitPerWlanAbgleichen();
    }

    if (DCF_EINBAUEN) dcfStarten();

    if (rtcZeitGueltig) {
        starteUhr();
    } else if (!rtcVorhanden) {
        zeigeHinweis("Keine Echtzeituhr (DS3231) gefunden.\nBitte Kabel pruefen.");
    } else if (DCF_EINBAUEN) {
        dcfHinweisAktualisieren();
    } else if (wlanKonfiguriert()) {
        zeigeHinweis("Zeit ungueltig.\nSuche WLAN fuer den Zeitabgleich ...");
    } else {
        zeigeHinweis("Zeit ungueltig.\nBitte DCF77 oder WLAN in konfiguration.h einschalten.");
    }
}

void loop() {
    if (DCF_EINBAUEN) {
        dcfVerarbeiten();
        if (dcfNeuGestellt) {
            dcfNeuGestellt = false;
            if (!uhrLaeuft) starteUhr();
        }
    }
    lv_timer_handler();
    delay(SCHLEIFE_PAUSE_MS);

    // Ohne gueltige Zeit: Hinweis aktuell halten, RTC suchen, ggf. WLAN versuchen
    static unsigned long letzterVersuch = 0;
    static unsigned long letzteHinweisZeit = 0;
    if (!uhrLaeuft) {
        if (DCF_EINBAUEN && rtcVorhanden && millis() - letzteHinweisZeit > 1000) {
            letzteHinweisZeit = millis();
            dcfHinweisAktualisieren();
        }
        // Mit DCF77 nur alle 5 Minuten WLAN versuchen: ein Minutentelegramm braucht 60 s Ruhe
        if (millis() - letzterVersuch > (DCF_EINBAUEN ? 300000UL : 30000UL)) {
            letzterVersuch = millis();
            if (!rtcVorhanden) {
                rtcVorhanden = rtc.begin();
                rtcPruefen();
            }
            if (rtcVorhanden && !rtcZeitGueltig && wlanKonfiguriert()) {
                zeigeHinweis("Zeit ungueltig.\nHole Zeit per WLAN ...");
                zeitPerWlanAbgleichen();
            }
            if (rtcZeitGueltig) starteUhr();
        }
    }
}

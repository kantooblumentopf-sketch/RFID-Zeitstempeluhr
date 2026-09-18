/*
 * RFID-Zeitstempeluhr
 * ESP32 + PN532 + DS3231 + OLED + WiFi/NTP
 *
 * Version: 1.0.0
 * Build: __DATE__ / __TIME__
 *
 * Hinweis:
 * Dieses Projekt ist auf den bisher besprochenen Hardwarestand zugeschnitten.
 * Display: SSD1306 128x64 I2C
 * RFID: PN532
 * RTC: DS3231
 *
 * Die konkreten GPIOs für Taster/Buzzer müssen ggf. an das verwendete
 * ESP32-2432S028-Board angepasst werden.
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_PN532.h>
#include <DS3231.h>
#include <WiFi.h>
#include <time.h>
#include <unordered_map>
#include <string>

#define VERSION "1.0.0"
#define BUILD_TIMESTAMP __DATE__ " " __TIME__

#define OLED_RESET       -1
#define OLED_ADDRESS     0x3C
#define OLED_WIDTH       128
#define OLED_HEIGHT      64

#define BUZZER_PIN       15
#define STATUS_BUTTON    4
#define KOMMEN_BUTTON    5
#define GEHEN_BUTTON     6

#define PN532_SDA        21
#define PN532_SCL        22

// ------------------------------------------------------------
// Konfiguration
// ------------------------------------------------------------

const char* ssid     = "DeinWLAN";
const char* password = "Passwort123";

// Haupt-RFID/Admin
const std::string ADMIN_UID = "AB12CD34";

// NTP-Konfiguration
const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 3600;       // Deutschland MEZ
const int DAYLIGHT_OFFSET_SEC = 3600;   // Sommerzeit

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
Adafruit_PN532 nfc(PN532_SDA, PN532_SCL);
DS3231 rtc;

// Status pro RFID
std::unordered_map<std::string, bool> mitarbeiterStatus;

// Tages-Gesamtzeit in Sekunden
std::unordered_map<std::string, uint32_t> anwesenheitsDauer;

// Startzeitpunkt der aktuell laufenden Anwesenheit als Unix-Zeit
std::unordered_map<std::string, time_t> anwesenheitsStart;

int mitarbeiterVorOrt = 0;

bool rtcGueltig = false;
bool ntpGueltig = false;

// ------------------------------------------------------------
// Hilfsfunktionen
// ------------------------------------------------------------

void beep(uint16_t frequency, uint16_t duration) {
    tone(BUZZER_PIN, frequency, duration);
    delay(duration);
    noTone(BUZZER_PIN);
}

void showMessage(const __FlashStringHelper* line1,
                 const __FlashStringHelper* line2 = nullptr,
                 uint16_t delayMs = 1200) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.println(line1);

    if (line2 != nullptr) {
        display.println();
        display.println(line2);
    }

    display.display();
    delay(delayMs);
}

bool syncTimeFromNTP() {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5000)) {
        ntpGueltig = false;
        return false;
    }

    ntpGueltig = true;
    Serial.println(F("NTP-Zeit erfolgreich synchronisiert."));
    return true;
}

bool getCurrentTime(time_t& now) {
    // Das ESP32-Systemzeitmodul wird nach NTP-Synchronisierung verwendet.
    // Dadurch bleibt die Zeit auch zwischen einzelnen RFID-Aktionen verfügbar.
    now = time(nullptr);

    if (now > 1000000000) {
        return true;
    }

    return false;
}

String formatDuration(uint32_t seconds) {
    uint32_t hours = seconds / 3600;
    uint32_t minutes = (seconds % 3600) / 60;
    uint32_t secs = seconds % 60;

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu",
             (unsigned long)hours,
             (unsigned long)minutes,
             (unsigned long)secs);

    return String(buffer);
}

String uidToString(uint8_t* uid, uint8_t uidLength) {
    String result;

    for (uint8_t i = 0; i < uidLength; i++) {
        char buffer[3];
        snprintf(buffer, sizeof(buffer), "%02X", uid[i]);
        result += buffer;
    }

    return result;
}

void updateDisplay() {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.println(F("RFID Zeiterfassung"));
    display.println();

    display.print(F("Anwesend: "));
    display.println(mitarbeiterVorOrt);

    display.print(F("Zeit: "));

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
        char timeBuffer[10];
        strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &timeinfo);
        display.println(timeBuffer);
    } else {
        display.println(F("--:--:--"));
    }

    display.display();
}

void resetDailyDataIfNeeded() {
    static int lastDay = -1;

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 100)) {
        return;
    }

    if (lastDay == -1) {
        lastDay = timeinfo.tm_yday;
        return;
    }

    if (timeinfo.tm_yday != lastDay) {
        anwesenheitsDauer.clear();
        lastDay = timeinfo.tm_yday;

        Serial.println(F("Neuer Tag: Tageszeiten wurden zurueckgesetzt."));
    }
}

uint32_t getCurrentDailyDuration(const std::string& uid) {
    uint32_t total = anwesenheitsDauer[uid];

    auto statusIt = mitarbeiterStatus.find(uid);
    if (statusIt == mitarbeiterStatus.end() || !statusIt->second) {
        return total;
    }

    auto startIt = anwesenheitsStart.find(uid);
    if (startIt == anwesenheitsStart.end()) {
        return total;
    }

    time_t now;
    if (!getCurrentTime(now)) {
        return total;
    }

    if (now >= startIt->second) {
        total += static_cast<uint32_t>(now - startIt->second);
    }

    return total;
}

// ------------------------------------------------------------
// Statusanzeige
// ------------------------------------------------------------

void showAllUsers() {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.println(F("Anwesende Mitarbeiter"));
    display.println();

    int count = 0;

    for (const auto& entry : mitarbeiterStatus) {
        if (!entry.second) {
            continue;
        }

        display.print(entry.first.c_str());
        display.println();

        count++;

        // Bei 64 Pixel Hoehe passen nur wenige Eintraege gleichzeitig.
        if (count >= 5) {
            break;
        }
    }

    if (count == 0) {
        display.println(F("Keine Mitarbeiter"));
    }

    display.display();
    delay(3000);
}

void showUserStatus(const std::string& uid) {
    uint32_t duration = getCurrentDailyDuration(uid);

    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.println(F("Mein Status"));
    display.println();

    display.print(F("RFID: "));
    display.println(uid.c_str());

    display.println();
    display.print(F("Heute:"));
    display.println();

    display.println(formatDuration(duration));

    if (mitarbeiterStatus[uid]) {
        display.println(F("ANWESEND"));
    } else {
        display.println(F("ABWESEND"));
    }

    display.display();
    delay(3000);
}

void showStatus() {
    showMessage(F("Status"), F("RFID vorhalten"), 500);

    uint8_t uid[7];
    uint8_t uidLength = 0;

    uint32_t startWait = millis();

    while (millis() - startWait < 10000) {
        if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A,
                                     uid, &uidLength, 100)) {
            continue;
        }

        std::string uidStr = uidToString(uid, uidLength).c_str();

        if (uidStr == ADMIN_UID) {
            beep(1800, 100);
            showAllUsers();
        } else {
            beep(1200, 100);

            if (mitarbeiterStatus.find(uidStr) == mitarbeiterStatus.end()) {
                mitarbeiterStatus[uidStr] = false;
                anwesenheitsDauer[uidStr] = 0;
            }

            showUserStatus(uidStr);
        }

        updateDisplay();
        return;
    }

    showMessage(F("Kein RFID erkannt"), nullptr, 1000);
    updateDisplay();
}

// ------------------------------------------------------------
// Kommen / Gehen
// ------------------------------------------------------------

void processAttendance(const std::string& uid, bool kommen) {
    // Admin-RFID darf nicht versehentlich als Mitarbeiter gestempelt werden.
    if (uid == ADMIN_UID) {
        beep(500, 250);
        showMessage(F("Admin-RFID"), F("Nur fuer Status"), 1500);
        updateDisplay();
        return;
    }

    if (mitarbeiterStatus.find(uid) == mitarbeiterStatus.end()) {
        mitarbeiterStatus[uid] = false;
        anwesenheitsDauer[uid] = 0;
    }

    bool aktuellAnwesend = mitarbeiterStatus[uid];

    if (kommen) {
        if (aktuellAnwesend) {
            showMessage(F("Bereits anwesend"), nullptr, 1200);
            updateDisplay();
            return;
        }

        time_t now;
        if (!getCurrentTime(now)) {
            showMessage(F("Keine gueltige Zeit"), F("NTP pruefen"), 1500);
            return;
        }

        mitarbeiterStatus[uid] = true;
        anwesenheitsStart[uid] = now;
        mitarbeiterVorOrt++;

        beep(1800, 120);

        Serial.print(F("Kommen: "));
        Serial.println(uid.c_str());

        showMessage(F("Kommen gebucht"), nullptr, 1000);
    } else {
        if (!aktuellAnwesend) {
            showMessage(F("Nicht anwesend"), nullptr, 1200);
            updateDisplay();
            return;
        }

        time_t now;
        if (!getCurrentTime(now)) {
            showMessage(F("Keine gueltige Zeit"), F("NTP pruefen"), 1500);
            return;
        }

        auto startIt = anwesenheitsStart.find(uid);

        if (startIt != anwesenheitsStart.end() && now >= startIt->second) {
            anwesenheitsDauer[uid] +=
                static_cast<uint32_t>(now - startIt->second);
        }

        mitarbeiterStatus[uid] = false;
        anwesenheitsStart.erase(uid);

        if (mitarbeiterVorOrt > 0) {
            mitarbeiterVorOrt--;
        }

        beep(900, 120);

        Serial.print(F("Gehen: "));
        Serial.println(uid.c_str());

        showMessage(F("Gehen gebucht"), nullptr, 1000);
    }

    updateDisplay();
}

void scanRFID(bool kommen) {
    showMessage(kommen ? F("Kommen") : F("Gehen"),
                F("RFID vorhalten"), 500);

    uint8_t uid[7];
    uint8_t uidLength = 0;

    uint32_t startWait = millis();

    while (millis() - startWait < 10000) {
        if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443,
                                     uid, &uidLength, 100)) {
            continue;
        }

        String uidString = uidToString(uid, uidLength);
        std::string uidStd = uidString.c_str();

        Serial.print(F("RFID erkannt: "));
        Serial.println(uidString);

        processAttendance(uidStd, kommen);
        return;
    }

    showMessage(F("Kein RFID erkannt"), nullptr, 1000);
    updateDisplay();
}

// ------------------------------------------------------------
// Setup
// ------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println(F("================================"));
    Serial.println(F("RFID Zeiterfassungssystem"));
    Serial.print(F("Version: "));
    Serial.println(VERSION);
    Serial.print(F("Build: "));
    Serial.println(BUILD_TIMESTAMP);
    Serial.println(F("================================"));

    Wire.begin(PN532_SDA, PN532_SCL);

    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(STATUS_BUTTON, INPUT_PULLUP);
    pinMode(KOMMEN_BUTTON, INPUT_PULLUP);
    pinMode(GEHEN_BUTTON, INPUT_PULLUP);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println(F("Fehler: OLED nicht gefunden!"));
        while (true) {
            delay(1000);
        }
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(F("RFID Zeiterfassung"));
    display.println(F("Start..."));
    display.display();

    Serial.println(F("Initialisiere PN532..."));

    nfc.begin();

    uint32_t versiondata = nfc.getFirmwareVersion();

    if (!versiondata) {
        Serial.println(F("Fehler: PN532 nicht erkannt!"));
        showMessage(F("PN532 Fehler"), F("RFID nicht erkannt"), 2000);
        while (true) {
            delay(1000);
        }
    }

    nfc.SAMConfig();

    Serial.println(F("PN532 OK"));

    // Die verwendete DS3231-Bibliothek besitzt laut bisherigem Projektstand
    // oscillatorCheck(). Ein rtc.begin() wird deshalb bewusst NICHT verwendet.
    rtcGueltig = rtc.oscillatorCheck();

    if (rtcGueltig) {
        Serial.println(F("DS3231: Oszillator OK"));
    } else {
        Serial.println(F("DS3231: keine gueltige Uhr erkannt/gestoppt."));
    }

    // NTP ist der Laufzeit-Fallback.
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    Serial.print(F("Verbinde mit WLAN"));

    uint32_t wifiStart = millis();

    while (WiFi.status() != WL_CONNECTED &&
           millis() - wifiStart < 15000) {
        delay(500);
        Serial.print('.');
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F("WLAN verbunden."));
        Serial.print(F("IP: "));
        Serial.println(WiFi.localIP());

        if (!rtcGueltig) {
            syncTimeFromNTP();
        } else {
            // NTP kann die Systemzeit ebenfalls initialisieren.
            syncTimeFromNTP();
        }
    } else {
        Serial.println(F("WLAN nicht verbunden."));

        if (!rtcGueltig) {
            Serial.println(F("WARNUNG: Keine Zeitquelle verfuegbar."));
        }
    }

    beep(1800, 100);
    updateDisplay();
}

// ------------------------------------------------------------
// Loop
// ------------------------------------------------------------

void loop() {
    resetDailyDataIfNeeded();

    if (digitalRead(STATUS_BUTTON) == LOW) {
        delay(30);

        if (digitalRead(STATUS_BUTTON) == LOW) {
            showStatus();

            while (digitalRead(STATUS_BUTTON) == LOW) {
                delay(10);
            }
        }
    }

    if (digitalRead(KOMMEN_BUTTON) == LOW) {
        delay(30);

        if (digitalRead(KOMMEN_BUTTON) == LOW) {
            scanRFID(true);

            while (digitalRead(KOMMEN_BUTTON) == LOW) {
                delay(10);
            }
        }
    }

    if (digitalRead(GEHEN_BUTTON) == LOW) {
        delay(30);

        if (digitalRead(GEHEN_BUTTON) == LOW) {
            scanRFID(false);

            while (digitalRead(GEHEN_BUTTON) == LOW) {
                delay(10);
            }
        }
    }

    updateDisplay();
    delay(100);
}

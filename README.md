# RFID-Zeitstempeluhr

ESP32-basierte RFID-Zeiterfassung mit:

- PN532 RFID-Leser
- DS3231 RTC
- WLAN / NTP-Zeitquelle
- SSD1306 OLED 128x64
- Kommen-Taste
- Gehen-Taste
- Status-Taste
- Admin-RFID
- Tages-Anwesenheitszeit
- Buzzer
- Firmware-Version und Build-Zeit im Serial Monitor

## Aktueller Stand

**Firmware-Version:** 1.0.0

Der Sketch ist als zusammenhängender Ausgangspunkt für das bisher entwickelte Projekt zusammengestellt.

### Bedienung

**Kommen**

1. Taste „Kommen“ drücken.
2. RFID-Karte vor den PN532 halten.
3. Die Anwesenheit wird gestartet.

**Gehen**

1. Taste „Gehen“ drücken.
2. RFID-Karte vor den PN532 halten.
3. Die laufende Anwesenheitszeit wird zur Tageszeit addiert.

**Status**

1. Taste „Status“ drücken.
2. RFID-Karte vor den PN532 halten.
3. Ein normaler Mitarbeiter sieht seine Tageszeit.
4. Die Admin-RFID zeigt die aktuell anwesenden Mitarbeiter.

## Zeit

Der DS3231 wird über `oscillatorCheck()` geprüft.

Für die Systemzeit wird zusätzlich NTP verwendet, sobald WLAN verfügbar ist.

NTP:
`pool.ntp.org`

Deutschland:
- UTC+1 im Winter
- UTC+2 im Sommer

## Hardware

Bisher angenommene Anschlüsse:

| Funktion | GPIO |
|---|---:|
| I2C SDA | 21 |
| I2C SCL | 22 |
| Buzzer | 15 |
| Status-Taste | 4 |
| Kommen-Taste | 5 |
| Gehen-Taste | 6 |

**Wichtig:** GPIO 6 ist bei vielen klassischen ESP32-Modulen mit dem internen SPI-Flash verbunden und darf dort nicht als normaler Taster verwendet werden. Beim ESP32-2432S028 müssen die tatsächlich verfügbaren GPIOs des konkreten Boards geprüft und ggf. im Sketch geändert werden.

## Arduino-Bibliotheken

Benötigt werden:

- Adafruit SSD1306
- Adafruit PN532
- DS3231-Bibliothek passend zur verwendeten `oscillatorCheck()`-API
- ESP32 WiFi
- ESP32 Time

Die genauen Bibliotheksversionen sollten beim ersten Kompilieren geprüft werden.

## Konfiguration

WLAN und Admin-UID sind im aktuellen Ausgangssketch als Platzhalter vorhanden.

Für ein öffentliches GitHub-Repository sollte die echte WLAN-Konfiguration nicht eingecheckt werden.

Dafür liegt unter `config/config.example.h` eine Vorlage.

## Serial Monitor

Baudrate:

`115200`

Beim Start werden unter anderem ausgegeben:

```text
RFID Zeiterfassungssystem
Version: 1.0.0
Build: <Datum> <Uhrzeit>
```

Damit kann kontrolliert werden, welche Firmware geflasht wurde.

## Bekannte Einschränkungen

Die Tagesdaten werden momentan nur im RAM gehalten.

Nach einem Neustart sind die Tageszeiten daher verloren.

Für eine spätere Version ist eine Speicherung auf der vorhandenen Micro-SD-Karte sinnvoll. Damit könnten beispielsweise RFID-Ereignisse wie

```text
Datum;Uhrzeit;UID;Kommen/Gehen
```

protokolliert und Tagesarbeitszeiten nach einem Neustart rekonstruiert werden.

## ESP32-2432S028

Der Benutzer verwendet ein ESP32-2432S028 mit SPI-Display und XPT2046-Touchscreen. Der hier enthaltene Sketch basiert für die Zeiterfassungslogik weiterhin auf dem zuvor verwendeten SSD1306-OLED-Stand.

Die Displaylogik kann in einem nächsten Schritt vollständig auf LovyanGFX + ILI9341V + XPT2046 umgestellt werden.

## Lizenz

Für dieses persönliche Projekt ist zunächst keine Lizenz festgelegt.

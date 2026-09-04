// M5Stack PaperColor (C151) device status dashboard.
// The first version is deliberately offline: it reports the device's own
// storage, power, RTC, sensor, and firmware state without requiring Wi-Fi.

#include <Arduino.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include <Preferences.h>
#include <esp_sleep.h>

#include "device_status_ui.h"
#include "m5pm1_power.h"
#include "rx8130_rtc.h"
#include "sht40_driver.h"

static constexpr uint32_t SAMPLE_INTERVAL_MS = 30000;
static constexpr uint32_t DISPLAY_INTERVAL_MS = 300000;
static constexpr uint32_t STORAGE_SCAN_LIMIT = 512;
static constexpr int SD_SCK = 15;
static constexpr int SD_MOSI = 13;
static constexpr int SD_MISO = 14;
static constexpr int SD_CS = 47;
static constexpr int32_t RTC_TZ_OFFSET = 8 * 3600;
// Measured from build_time.py's timestamp through upload/reset to setup().
static constexpr uint32_t RTC_BUILD_LAG_SEC = 16;

#ifndef BUILD_UNIX_TIME
#define BUILD_UNIX_TIME 0
#endif

M5GFX &display = M5.Display;
SHT40Sensor sht40;
RX8130RTC rtc;
M5PM1Power pm1;
DeviceStatusUI ui;
StorageStatus storage;

uint32_t lastSample = 0;
uint32_t lastDisplay = 0;
bool detailsPage = false;

// Follow M5Stack's PaperColor speaker example: tone() initialises the
// Speaker_Class lazily, and the audio task must remain alive long enough for
// the ES8311/amp to emit each note.  Each cue is a four-note, two-second
// melody; do not stop/end the audio task here.
void playChime(bool startup) {
    if (!M5.Speaker.isEnabled()) return;
    if (startup) {
        M5.Speaker.tone(4000, 380);
        M5.delay(500);
        M5.Speaker.tone(5000, 380);
        M5.delay(500);
        M5.Speaker.tone(6000, 380);
        M5.delay(500);
        M5.Speaker.tone(8000, 380);
        M5.delay(500);
    } else {
        M5.Speaker.tone(8000, 380);
        M5.delay(500);
        M5.Speaker.tone(6000, 380);
        M5.delay(500);
        M5.Speaker.tone(5000, 380);
        M5.delay(500);
        M5.Speaker.tone(4000, 380);
        M5.delay(500);
    }
}

static const char *cardTypeName(uint8_t type) {
    switch (type) {
        case CARD_MMC:  return "MMC";
        case CARD_SD:   return "SDSC";
        case CARD_SDHC: return "SDHC";
        default:        return "UNKNOWN";
    }
}

// The RX8130CE keeps time only while its backup supply remains available.
// When it reports a lost/invalid value, seed it from this freshly-built
// offline firmware, then verify the write before showing the time as valid.
bool setRTCFromBuildTime() {
    if (BUILD_UNIX_TIME <= 1600000000) return false;

    time_t buildTime = (time_t)BUILD_UNIX_TIME + RTC_TZ_OFFSET + RTC_BUILD_LAG_SEC;
    struct tm localTime;
    gmtime_r(&buildTime, &localTime);
    bool ok = rtc.setDateTime(localTime);
    Serial.printf("RTC set from build time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
                  localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
    return ok;
}

// A newly-uploaded build has a different injected timestamp.  Record it only
// after RTC write verification, so the build time corrects this flash once but
// normal rebooting keeps the RTC's continuously advancing value.
bool isFirstBootOfThisBuild() {
    if (BUILD_UNIX_TIME <= 1600000000) return false;
    Preferences preferences;
    if (!preferences.begin("status-rtc", false)) return true;
    bool isNew = preferences.getULong("rtc_build", 0) != BUILD_UNIX_TIME;
    preferences.end();
    return isNew;
}

void rememberCurrentBuildTime() {
    if (BUILD_UNIX_TIME <= 1600000000) return;
    Preferences preferences;
    if (preferences.begin("status-rtc", false)) {
        preferences.putULong("rtc_build", BUILD_UNIX_TIME);
        preferences.end();
    }
}

bool rtcDateIsPlausible(int year) {
    return year >= 2024 && year <= 2090;
}

bool ensureRTCTime() {
    int year, month, day, hour, minute, second, weekday;
    bool readable = rtc.getDateTime(year, month, day, hour, minute, second, weekday);
    bool voltageLow = rtc.isVoltageLow();
    bool firstBootOfBuild = isFirstBootOfThisBuild();
    bool needsRestore = !readable || voltageLow || !rtcDateIsPlausible(year) || firstBootOfBuild;

    Serial.printf("RTC before restore: readable=%d VLF=%d fresh-build=%d year=%d driver=%s\n",
                  readable, voltageLow, firstBootOfBuild, readable ? year : 0,
                  rtc.usingM5Driver() ? "M5.Rtc" : "raw");
    if (!needsRestore) return true;

    setRTCFromBuildTime();
    readable = rtc.getDateTime(year, month, day, hour, minute, second, weekday);
    if (readable && rtcDateIsPlausible(year)) {
        rememberCurrentBuildTime();
        Serial.println("RTC restore verified");
        return true;
    }

    // Some M5Unified versions can see the RTC but fail to persist a write.
    // Retry once using the RX8130CE register-level fallback in our wrapper.
    Serial.println("RTC restore retry via raw registers");
    rtc.setForceRaw(true);
    rtc.begin();
    setRTCFromBuildTime();
    readable = rtc.getDateTime(year, month, day, hour, minute, second, weekday);
    bool restored = readable && rtcDateIsPlausible(year);
    if (restored) rememberCurrentBuildTime();
    Serial.printf("RTC raw restore: %s\n", restored ? "verified" : "failed");
    return restored;
}

void scanStorage() {
    storage = StorageStatus{};

    // PaperColor's EPD and microSD share SCK/MOSI.  M5Unified enables the
    // M5PM1 TF-card power rail during M5.begin(); initialise the shared bus
    // using the official PaperColor pin mapping before mounting the card.
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, SPI, 25000000)) {
        Serial.println("microSD: mount failed or no card");
        return;
    }

    uint8_t type = SD.cardType();
    if (type == CARD_NONE) {
        Serial.println("microSD: no card");
        return;
    }

    storage.mounted = true;
    storage.cardType = type;
    storage.totalBytes = SD.totalBytes();
    storage.usedBytes = SD.usedBytes();

    File root = SD.open("/");
    if (root && root.isDirectory()) {
        for (uint32_t count = 0; count < STORAGE_SCAN_LIMIT; ++count) {
            File entry = root.openNextFile();
            if (!entry) break;
            ++storage.rootFiles;
            entry.close();
            if (count + 1 == STORAGE_SCAN_LIMIT) storage.scanLimited = true;
        }
        root.close();
    }

    Serial.printf("microSD: %s total=%llu used=%llu root-files=%lu%s\n",
                  cardTypeName(type),
                  (unsigned long long)storage.totalBytes,
                  (unsigned long long)storage.usedBytes,
                  (unsigned long)storage.rootFiles,
                  storage.scanLimited ? "+" : "");
}

DeviceStatusData collectStatus() {
    DeviceStatusData data;
    data.sensorOK = sht40.read(data.temperature, data.humidity);
    data.rtcOK = rtc.getDateTime(data.year, data.month, data.day,
                                 data.hour, data.minute, data.second, data.weekday);
    data.powerOK = pm1.begin();
    data.batteryMV = pm1.getBatteryVoltage();
    data.batteryPct = pm1.getBatteryPercent();
    data.vbusMV = pm1.getVBUSVoltage();
    data.chargeState = pm1.getChargeState();
    data.uptimeSeconds = millis() / 1000UL;
    data.freeHeap = ESP.getFreeHeap();
    data.freePsram = ESP.getFreePsram();
    data.storage = storage;
    return data;
}

String updateLabel(const DeviceStatusData &data) {
    char value[32];
    if (data.rtcOK) {
        snprintf(value, sizeof(value), "Updated %02d:%02d:%02d", data.hour, data.minute, data.second);
    } else {
        snprintf(value, sizeof(value), "Updated after boot");
    }
    return String(value);
}

void refreshDisplay(bool rescanStorage) {
    if (rescanStorage) scanStorage();
    DeviceStatusData data = collectStatus();
    Serial.printf("[status] T=%.1f H=%.1f bat=%u%%/%umV sd=%d heap=%lu psram=%lu page=%s\n",
                  data.temperature, data.humidity, data.batteryPct, data.batteryMV,
                  data.storage.mounted, (unsigned long)data.freeHeap,
                  (unsigned long)data.freePsram, detailsPage ? "details" : "overview");
    ui.render(data, detailsPage, updateLabel(data));
    display.display();
    lastDisplay = millis();
}

void setup() {
    auto config = M5.config();
    config.serial_baudrate = 115200;
    // PlatformIO's generic ESP32-S3 board cannot identify PaperColor by name.
    // Make M5Unified select the C151 pin map (ES8311 + I2S speaker pins)
    // whenever automatic display detection does not provide a board identity.
    config.fallback_board = m5::board_t::board_M5PaperColor;
    config.internal_spk = true;
    M5.begin(config);
    display.setAutoDisplay(false);
    ui.begin(&display);

    Serial.println("\nM5Stack PaperColor Device Status Dashboard");
    Serial.printf("Display %dx%d, EPD=%d\n", display.width(), display.height(), display.isEPD());
    Serial.printf("Internal I2C SDA=%d SCL=%d\n", M5.In_I2C.getSDA(), M5.In_I2C.getSCL());

    Serial.printf("M5PM1: %s\n", pm1.begin() ? "OK" : "FAIL");
    bool rtcFound = rtc.begin();
    Serial.printf("RX8130CE: %s\n", rtcFound ? "found" : "not found");
    if (rtcFound) {
        Serial.printf("RX8130CE time: %s\n", ensureRTCTime() ? "READY" : "ERROR");
    }
    Serial.printf("SHT40: %s\n", sht40.begin() ? "OK" : "FAIL");
    playChime(true);

    display.fillScreen(DASH_BG);
    display.display();
    refreshDisplay(true);
    lastSample = millis();
}

void loop() {
    M5.update();
    uint32_t now = millis();

    if (M5.BtnA.wasPressed()) {
        Serial.println("BtnA: refresh dashboard and rescan SD");
        refreshDisplay(true);
        lastSample = millis();
    } else if (M5.BtnB.wasPressed()) {
        detailsPage = !detailsPage;
        Serial.printf("BtnB: %s page\n", detailsPage ? "details" : "overview");
        refreshDisplay(true);
        lastSample = millis();
    } else if (M5.BtnC.wasPressed()) {
        Serial.println("BtnC: power off");
        playChime(false);
        display.fillScreen(DASH_BG);
        display.display();
        delay(200);
        M5.Power.powerOff();
        esp_deep_sleep_start();
    } else if (now - lastDisplay >= DISPLAY_INTERVAL_MS) {
        refreshDisplay(true);
        lastSample = millis();
    } else if (now - lastSample >= SAMPLE_INTERVAL_MS) {
        // Read sensors between e-ink updates; a new screen is still limited to
        // the five-minute policy unless the user requests a refresh.
        DeviceStatusData sampled = collectStatus();
        Serial.printf("[sample] T=%.1f H=%.1f bat=%u%% sd=%d\n",
                      sampled.temperature, sampled.humidity,
                      sampled.batteryPct, sampled.storage.mounted);
        lastSample = millis();
    }

    delay(50);
}

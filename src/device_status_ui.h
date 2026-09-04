#pragma once

#include <Arduino.h>
#include <M5GFX.h>

// PaperColor supports only a small set of saturated colors.  Keep text and
// card edges in those colors so the result remains crisp on the Spectra 6 EPD.
#define DASH_BG      0xFFFF
#define DASH_BLACK   0x0000
#define DASH_WHITE   0xFFFF
#define DASH_RED     0xF800
#define DASH_GREEN   0x07E0
#define DASH_BLUE    0x001F
#define DASH_YELLOW  0xFFE0

static constexpr int DASH_W = 400;
static constexpr int DASH_H = 600;
static constexpr int HEADER_H = 68;
static constexpr int CARD_W = 188;
static constexpr int CARD_H = 190;
static constexpr int LEFT_X = 6;
static constexpr int RIGHT_X = 206;
static constexpr int ROW1_Y = 76;
static constexpr int ROW2_Y = 274;
static constexpr int FOOT_Y = 472;

struct StorageStatus {
    bool mounted = false;
    bool scanLimited = false;
    uint8_t cardType = 0;
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    uint32_t rootFiles = 0;
};

struct DeviceStatusData {
    bool sensorOK = false;
    bool rtcOK = false;
    bool powerOK = false;
    float temperature = 0.0f;
    float humidity = 0.0f;
    uint8_t batteryPct = 0;
    uint16_t batteryMV = 0;
    uint16_t vbusMV = 0;
    int8_t chargeState = -1;
    int year = 2000, month = 1, day = 1;
    int hour = 0, minute = 0, second = 0, weekday = 0;
    uint32_t uptimeSeconds = 0;
    uint32_t freeHeap = 0;
    uint32_t freePsram = 0;
    StorageStatus storage;
};

class DeviceStatusUI {
public:
    void begin(M5GFX *display) { _display = display; }

    void render(const DeviceStatusData &data, bool detailsPage, const String &updated) {
        _display->fillScreen(DASH_BG);
        drawHeader(data, detailsPage);
        if (detailsPage) drawDetails(data);
        else             drawOverview(data);
        drawFooter(data, detailsPage, updated);
    }

private:
    M5GFX *_display = nullptr;

    void text(const String &value, int x, int y, const lgfx::IFont *font,
              uint16_t color = DASH_BLACK, textdatum_t datum = top_left,
              float scale = 1.0f) {
        _display->setFont(font);
        _display->setTextSize(scale);
        _display->setTextColor(color);
        _display->setTextDatum(datum);
        _display->drawString(value, x, y);
    }

    // Dynamic values such as SD-card capacities must never cross a card edge.
    // The normal labels below are deliberately short; this is a final safety
    // net for unusually large cards or future firmware-version strings.
    String fit(const String &value, const lgfx::IFont *font, int maxWidth,
               float scale = 1.0f) {
        _display->setFont(font);
        _display->setTextSize(scale);
        if (_display->textWidth(value) <= maxWidth) return value;
        String clipped = value;
        while (clipped.length() > 1 && _display->textWidth(clipped + "...") > maxWidth) {
            clipped.remove(clipped.length() - 1);
        }
        return clipped + "...";
    }

    void cardText(const String &value, int x, int y, const lgfx::IFont *font,
                  uint16_t color = DASH_BLACK, int maxWidth = CARD_W - 28,
                  float scale = 1.0f) {
        text(fit(value, font, maxWidth, scale), x, y, font, color, top_left, scale);
    }

    void card(int x, int y, uint16_t accent) {
        _display->fillRoundRect(x, y, CARD_W, CARD_H, 8, DASH_WHITE);
        _display->fillRoundRect(x, y, CARD_W, 6, 3, accent);
        _display->drawRoundRect(x, y, CARD_W, CARD_H, 8, DASH_BLACK);
    }

    void progress(int x, int y, int width, uint8_t percent, uint16_t color) {
        _display->drawRoundRect(x, y, width, 20, 4, DASH_BLACK);
        int fill = (width - 4) * min<int>(percent, 100) / 100;
        if (fill > 0) _display->fillRoundRect(x + 2, y + 2, fill, 16, 3, color);
    }

    static String bytes(uint64_t value) {
        char buf[24];
        if (value >= 1024ULL * 1024ULL * 1024ULL) {
            snprintf(buf, sizeof(buf), "%.1f GB", value / 1073741824.0);
        } else if (value >= 1024ULL * 1024ULL) {
            snprintf(buf, sizeof(buf), "%.1f MB", value / 1048576.0);
        } else {
            snprintf(buf, sizeof(buf), "%llu KB", (unsigned long long)(value / 1024ULL));
        }
        return String(buf);
    }

    static String uptime(uint32_t seconds) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lud %02luh %02lum",
                 (unsigned long)(seconds / 86400UL),
                 (unsigned long)((seconds / 3600UL) % 24UL),
                 (unsigned long)((seconds / 60UL) % 60UL));
        return String(buf);
    }

    static String cardType(uint8_t type) {
        switch (type) {
            case CARD_MMC:  return "MMC";
            case CARD_SD:   return "SDSC";
            case CARD_SDHC: return "SDHC";
            default:        return "UNKNOWN";
        }
    }

    uint16_t batteryColor(uint8_t percent) {
        return percent <= 20 ? DASH_RED : (percent <= 50 ? DASH_YELLOW : DASH_GREEN);
    }

    uint8_t storagePercent(const StorageStatus &storage) {
        if (!storage.mounted || storage.totalBytes == 0) return 0;
        uint64_t pct = storage.usedBytes * 100ULL / storage.totalBytes;
        return (uint8_t)min<uint64_t>(pct, 100);
    }

    void drawHeader(const DeviceStatusData &data, bool detailsPage) {
        _display->fillRect(0, 0, DASH_W, HEADER_H, DASH_BLACK);
        text(detailsPage ? "Device Details" : "Device Status", 12, 8,
             &fonts::FreeSansBold18pt7b, DASH_WHITE);

        char timeBuf[18];
        if (data.rtcOK) snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", data.hour, data.minute);
        else            snprintf(timeBuf, sizeof(timeBuf), "--:--");
        text(timeBuf, 388, 9, &fonts::FreeSansBold18pt7b, DASH_WHITE, top_right);

        String summary = data.storage.mounted ? "C151 | SD OK" : "C151 | SD ERR";
        text(summary, 12, 44, &fonts::FreeSans12pt7b, DASH_WHITE);
        _display->fillCircle(382, 52, 5,
                             (data.powerOK && data.rtcOK) ? DASH_GREEN : DASH_RED);
    }

    void drawPowerCard(int x, int y, const DeviceStatusData &data) {
        card(x, y, DASH_GREEN);
        text("POWER", x + 14, y + 16, &fonts::FreeSansBold9pt7b);
        String source = data.chargeState > 0 ? "CHG" : (data.vbusMV > 4000 ? "USB" : "BAT");
        text(source, x + CARD_W - 14, y + 16, &fonts::FreeSansBold9pt7b,
             source == "CHG" ? DASH_GREEN : (source == "USB" ? DASH_BLUE : DASH_BLACK), top_right);

        char value[16];
        snprintf(value, sizeof(value), "%u%%", data.batteryPct);
        text(value, x + 14, y + 42, &fonts::FreeSansBold24pt7b, batteryColor(data.batteryPct));

        char voltage[32];
        snprintf(voltage, sizeof(voltage), "%.2fV | %s", data.batteryMV / 1000.0f, source.c_str());
        cardText(voltage, x + 14, y + 108, &fonts::FreeSans12pt7b);
        cardText(data.vbusMV > 4000 ? "External power" : "Battery power", x + 14, y + 140,
                 &fonts::FreeSans9pt7b);
        progress(x + 14, y + 158, CARD_W - 28, data.batteryPct, batteryColor(data.batteryPct));
    }

    void drawStorageCard(int x, int y, const DeviceStatusData &data) {
        card(x, y, DASH_BLUE);
        text("STORAGE", x + 14, y + 16, &fonts::FreeSansBold9pt7b);
        text(data.storage.mounted ? "SD OK" : "NO SD", x + CARD_W - 14, y + 16,
             &fonts::FreeSansBold9pt7b, data.storage.mounted ? DASH_GREEN : DASH_RED, top_right);

        if (!data.storage.mounted) {
            text("No card", x + 14, y + 52, &fonts::FreeSansBold18pt7b, DASH_RED);
            cardText("Insert microSD card", x + 14, y + 110, &fonts::FreeSans12pt7b);
            cardText("BtnA refreshes", x + 14, y + 146, &fonts::FreeSans9pt7b);
            return;
        }

        uint8_t used = storagePercent(data.storage);
        char value[16];
        snprintf(value, sizeof(value), "%u%%", used);
        text(value, x + 14, y + 42, &fonts::FreeSansBold24pt7b, DASH_BLUE);
        cardText(bytes(data.storage.usedBytes) + "/" + bytes(data.storage.totalBytes), x + 14, y + 108,
                 &fonts::FreeSans12pt7b, DASH_BLACK, CARD_W - 28, 0.76f);
        cardText("Free " + bytes(data.storage.totalBytes - data.storage.usedBytes), x + 14, y + 140,
                 &fonts::FreeSans9pt7b);
        progress(x + 14, y + 158, CARD_W - 28, used, DASH_BLUE);
    }

    void drawEnvironmentCard(int x, int y, const DeviceStatusData &data) {
        card(x, y, DASH_YELLOW);
        text("ENVIRONMENT", x + 14, y + 16, &fonts::FreeSansBold9pt7b);
        text(data.sensorOK ? "SHT40" : "ERROR", x + CARD_W - 14, y + 16,
             &fonts::FreeSansBold9pt7b, data.sensorOK ? DASH_GREEN : DASH_RED, top_right);

        if (!data.sensorOK) {
            text("Sensor unavailable", x + 14, y + 60, &fonts::FreeSansBold12pt7b, DASH_RED);
            text("Check internal I2C", x + 14, y + 110, &fonts::FreeSans12pt7b);
            return;
        }
        char line[32];
        snprintf(line, sizeof(line), "%.1f C", data.temperature);
        text(line, x + 14, y + 46, &fonts::FreeSansBold18pt7b,
             data.temperature >= 30.0f ? DASH_RED : DASH_BLACK, top_left, 0.82f);
        snprintf(line, sizeof(line), "%.1f %% RH", data.humidity);
        text(line, x + 14, y + 92, &fonts::FreeSansBold18pt7b, DASH_BLUE, top_left, 0.82f);
        const char *comfort = (data.temperature >= 18.0f && data.temperature <= 28.0f &&
                               data.humidity >= 30.0f && data.humidity <= 70.0f)
                                  ? "Comfortable" : "Outside range";
        cardText(comfort, x + 14, y + 144, &fonts::FreeSans9pt7b,
                 String(comfort).startsWith("Comfort") ? DASH_GREEN : DASH_RED);
    }

    void drawSystemCard(int x, int y, const DeviceStatusData &data) {
        card(x, y, DASH_RED);
        text("SYSTEM", x + 14, y + 16, &fonts::FreeSansBold9pt7b);
        text("v0.1.0", x + CARD_W - 14, y + 16, &fonts::FreeSansBold9pt7b, DASH_BLUE, top_right);
        text("ESP32-S3R8", x + 14, y + 48, &fonts::FreeSansBold18pt7b,
             DASH_BLACK, top_left, 0.72f);
        cardText("Firmware status", x + 14, y + 94, &fonts::FreeSans12pt7b);
        cardText("Up " + uptime(data.uptimeSeconds), x + 14, y + 128, &fonts::FreeSans12pt7b);
        text("RTC " + String(data.rtcOK ? "OK" : "ERROR"), x + 14, y + 158,
             &fonts::FreeSans9pt7b, data.rtcOK ? DASH_GREEN : DASH_RED);
    }

    void drawOverview(const DeviceStatusData &data) {
        drawSystemCard(LEFT_X, ROW1_Y, data);
        drawStorageCard(RIGHT_X, ROW1_Y, data);
        drawEnvironmentCard(LEFT_X, ROW2_Y, data);
        drawPowerCard(RIGHT_X, ROW2_Y, data);
    }

    void drawDetails(const DeviceStatusData &data) {
        card(LEFT_X, ROW1_Y, DASH_BLUE);
        text("SD CARD", LEFT_X + 14, ROW1_Y + 16, &fonts::FreeSansBold9pt7b);
        if (data.storage.mounted) {
            text(cardType(data.storage.cardType), LEFT_X + 14, ROW1_Y + 48, &fonts::FreeSansBold18pt7b, DASH_BLUE);
            cardText("Total " + bytes(data.storage.totalBytes), LEFT_X + 14, ROW1_Y + 92, &fonts::FreeSans12pt7b);
            cardText("Used " + bytes(data.storage.usedBytes), LEFT_X + 14, ROW1_Y + 122, &fonts::FreeSans12pt7b);
            cardText("Root " + String(data.storage.rootFiles) + (data.storage.scanLimited ? "+ files" : " files"),
                     LEFT_X + 14, ROW1_Y + 154, &fonts::FreeSans9pt7b);
        } else {
            text("Not mounted", LEFT_X + 14, ROW1_Y + 62, &fonts::FreeSansBold18pt7b, DASH_RED);
            cardText("SCK15 MOSI13", LEFT_X + 14, ROW1_Y + 116, &fonts::FreeSans12pt7b);
            cardText("MISO14 CS47", LEFT_X + 14, ROW1_Y + 148, &fonts::FreeSans12pt7b);
        }

        card(RIGHT_X, ROW1_Y, DASH_GREEN);
        text("HEALTH", RIGHT_X + 14, ROW1_Y + 16, &fonts::FreeSansBold9pt7b);
        healthLine("SHT40", data.sensorOK, RIGHT_X + 14, ROW1_Y + 54);
        healthLine("RX8130 RTC", data.rtcOK, RIGHT_X + 14, ROW1_Y + 90);
        healthLine("M5PM1", data.powerOK, RIGHT_X + 14, ROW1_Y + 126);
        healthLine("MICROSD", data.storage.mounted, RIGHT_X + 14, ROW1_Y + 162);

        card(LEFT_X, ROW2_Y, DASH_RED);
        text("MEMORY", LEFT_X + 14, ROW2_Y + 16, &fonts::FreeSansBold9pt7b);
        text(String(data.freeHeap / 1024UL) + " KB", LEFT_X + 14, ROW2_Y + 50,
             &fonts::FreeSansBold18pt7b);
        text("free heap", LEFT_X + 14, ROW2_Y + 84, &fonts::FreeSans9pt7b);
        text(String(data.freePsram / 1024UL / 1024UL) + " MB", LEFT_X + 14, ROW2_Y + 120,
             &fonts::FreeSansBold18pt7b);
        text("free PSRAM", LEFT_X + 14, ROW2_Y + 154, &fonts::FreeSans9pt7b);

        card(RIGHT_X, ROW2_Y, DASH_YELLOW);
        text("DEVICE", RIGHT_X + 14, ROW2_Y + 16, &fonts::FreeSansBold9pt7b);
        cardText("PaperColor", RIGHT_X + 14, ROW2_Y + 48, &fonts::FreeSansBold18pt7b,
                 DASH_BLACK, CARD_W - 28, 0.78f);
        cardText("C151 | 4in EPD", RIGHT_X + 14, ROW2_Y + 88, &fonts::FreeSans12pt7b);
        cardText("Build " __DATE__, RIGHT_X + 14, ROW2_Y + 122, &fonts::FreeSans9pt7b);
        cardText("SD scan on refresh", RIGHT_X + 14, ROW2_Y + 154, &fonts::FreeSans9pt7b);
    }

    void healthLine(const char *name, bool ok, int x, int y) {
        _display->fillCircle(x + 5, y + 6, 5, ok ? DASH_GREEN : DASH_RED);
        // Reserve the right edge for the status so a long device name can
        // never collide with it or leave the card boundary.
        cardText(name, x + 18, y - 4, &fonts::FreeSans9pt7b,
                 ok ? DASH_BLACK : DASH_RED, 104);
        text(ok ? "OK" : "ERR", x + 160, y - 4, &fonts::FreeSansBold9pt7b,
             ok ? DASH_GREEN : DASH_RED, top_right);
    }

    void drawFooter(const DeviceStatusData &data, bool detailsPage, const String &updated) {
        _display->drawFastHLine(6, FOOT_Y - 8, 388, DASH_BLACK);
        text(updated, 12, FOOT_Y, &fonts::FreeSansBold12pt7b, DASH_BLACK);
        if (data.rtcOK) {
            char date[24];
            snprintf(date, sizeof(date), "%04d-%02d-%02d", data.year, data.month, data.day);
            text(date, 388, FOOT_Y, &fonts::FreeSansBold12pt7b, DASH_BLUE, top_right);
            char refresh[18];
            snprintf(refresh, sizeof(refresh), "Last: %02d:%02d:%02d", data.hour, data.minute, data.second);
            text(refresh, 12, FOOT_Y + 30, &fonts::FreeSans9pt7b, DASH_BLACK);
        } else {
            text("Last: boot +" + uptime(data.uptimeSeconds), 12, FOOT_Y + 30,
                 &fonts::FreeSans9pt7b, DASH_BLACK);
        }
        text(detailsPage ? "A Refresh  B Overview  C Off" : "A Refresh  B Details  C Off",
             12, FOOT_Y + 58, &fonts::FreeSans12pt7b);
    }
};

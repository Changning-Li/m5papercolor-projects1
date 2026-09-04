#pragma once
// =============================================================
// SHT40 温湿度传感器驱动
//
// 走 M5Unified 的内部 I2C 总线 (M5.In_I2C, 实机: SDA=GPIO3 / SCL=GPIO2),
// 与 RX8130CE / M5PM1 共用同一条总线, 不再另开 Wire, 避免两个 I2C 外设
// 抢同一组引脚导致通讯失败。
// 地址: 0x44
// =============================================================

#include <Arduino.h>
#include <M5Unified.h>

#define SHT40_ADDR 0x44
#define SHT40_FREQ 400000

class SHT40Sensor {
public:
    bool begin() {
        // 软复位 (0x94), 有 ACK 即认为在线
        if (!writeCmd(0x94)) return false;
        delay(2);
        return true;
    }

    // 高精度测量: 命令 0xFD, 最大转换时间 ~10ms
    bool read(float &temperature, float &humidity) {
        if (!writeCmd(0xFD)) return false;
        delay(12);

        uint8_t buf[6] = {0};
        if (!M5.In_I2C.start(SHT40_ADDR, true, SHT40_FREQ)) return false;
        bool ok = M5.In_I2C.read(buf, 6, true);
        M5.In_I2C.stop();
        if (!ok) return false;

        // CRC 校验 (多项式 0x31, 初值 0xFF)
        if (!checkCRC(buf[0], buf[1], buf[2])) return false;
        if (!checkCRC(buf[3], buf[4], buf[5])) return false;

        uint16_t rawT = ((uint16_t)buf[0] << 8) | buf[1];
        uint16_t rawH = ((uint16_t)buf[3] << 8) | buf[4];

        temperature = -45.0f + 175.0f * (float)rawT / 65535.0f;
        humidity    =  -6.0f + 125.0f * (float)rawH / 65535.0f;

        if (humidity < 0.0f)   humidity = 0.0f;
        if (humidity > 100.0f) humidity = 100.0f;
        if (temperature < -40.0f || temperature > 125.0f) return false;
        return true;
    }

    // 读序列号, 用于确认传感器确实在线 (调试用)
    bool readSerial(uint32_t &sn) {
        if (!writeCmd(0x89)) return false;
        delay(2);
        uint8_t buf[6] = {0};
        if (!M5.In_I2C.start(SHT40_ADDR, true, SHT40_FREQ)) return false;
        bool ok = M5.In_I2C.read(buf, 6, true);
        M5.In_I2C.stop();
        if (!ok) return false;
        if (!checkCRC(buf[0], buf[1], buf[2])) return false;
        sn = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
             ((uint32_t)buf[3] << 8)  | buf[4];
        return true;
    }

private:
    bool writeCmd(uint8_t cmd) {
        if (!M5.In_I2C.start(SHT40_ADDR, false, SHT40_FREQ)) return false;
        bool ok = M5.In_I2C.write(&cmd, 1);
        M5.In_I2C.stop();
        return ok;
    }

    bool checkCRC(uint8_t b0, uint8_t b1, uint8_t crc) {
        uint8_t c = 0xFF;
        uint8_t data[2] = {b0, b1};
        for (int i = 0; i < 2; i++) {
            c ^= data[i];
            for (int b = 0; b < 8; b++) {
                c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
            }
        }
        return c == crc;
    }
};

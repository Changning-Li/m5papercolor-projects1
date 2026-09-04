#pragma once
// =============================================================
// RX8130CE 实时时钟封装
//
// 主路径: 直接使用 M5Unified 自带的 M5.Rtc (官方 RX8130 驱动, 已针对
//         PaperColor 适配, 内部总线实机为 SDA=GPIO3 / SCL=GPIO2)。
// 备用路径: 若 M5.Rtc 不可用, 则走 M5.In_I2C 直接读寄存器。
//         注意 RX8130CE 的时间寄存器从 0x10 开始 (SEC=0x10 ... YEAR=0x16),
//         星期寄存器是位编码 (bit0=周日 ... bit6=周六), 不是 1~7 的数值。
//         很多人按 0x00 起始的映射去读会一直得到 00:00, 这里按官方映射实现。
// 地址: 0x32
// =============================================================

#include <Arduino.h>
#include <M5Unified.h>

#define RX8130_ADDR 0x32
#define RX8130_FREQ 400000

// 官方映射下的寄存器地址
#define RX8130_REG_SEC   0x10
#define RX8130_REG_MIN   0x11
#define RX8130_REG_HOUR  0x12
#define RX8130_REG_WDAY  0x13
#define RX8130_REG_DAY   0x14
#define RX8130_REG_MONTH 0x15
#define RX8130_REG_YEAR  0x16
#define RX8130_REG_FLAG  0x1D
#define RX8130_REG_CTRL  0x1E
#define RX8130_FLAG_VLF  0x80

class RX8130RTC {
public:
    bool begin() {
        if (_forceRaw) { _useM5 = false; }
        else           { _useM5 = M5.Rtc.isEnabled(); }
        // 主路径确认 / 回退到直接寄存器访问
        return M5.In_I2C.scanID(RX8130_ADDR, RX8130_FREQ);
    }

    bool usingM5Driver() const { return _useM5; }

    // 强制走「直接寄存器」路径 (M5.Rtc 写不进去时的兜底)
    void setForceRaw(bool raw) {
        _forceRaw = raw;
        if (raw) _useM5 = false;
    }

    // 电池掉电 / 时钟未初始化标志
    bool isVoltageLow() {
        if (_useM5) return M5.Rtc.getVoltLow();
        uint8_t flag = 0;
        if (!M5.In_I2C.readRegister(RX8130_ADDR, RX8130_REG_FLAG,
                                    &flag, 1, RX8130_FREQ)) return true;
        return (flag & RX8130_FLAG_VLF) != 0;
    }

    bool getDateTime(int &year, int &month, int &day,
                     int &hour, int &minute, int &second, int &weekday) {
        if (_useM5) {
            m5::rtc_datetime_t dt;
            if (!M5.Rtc.getDateTime(&dt)) return false;
            year    = dt.date.year;
            month   = dt.date.month;
            day     = dt.date.date;
            weekday = dt.date.weekDay;      // 0=周日
            hour    = dt.time.hours;
            minute  = dt.time.minutes;
            second  = dt.time.seconds;
            return true;
        }

        uint8_t b[7] = {0};
        if (!M5.In_I2C.readRegister(RX8130_ADDR, RX8130_REG_SEC, b, 7, RX8130_FREQ)) return false;
        if (!validBCD(b[0] & 0x7F) || !validBCD(b[1] & 0x7F) || !validBCD(b[2] & 0x3F)) return false;

        second  = bcd2dec(b[0] & 0x7F);
        minute  = bcd2dec(b[1] & 0x7F);
        hour    = bcd2dec(b[2] & 0x3F);
        weekday = bitToWeekday(b[3]);
        day     = bcd2dec(b[4] & 0x3F);
        month   = bcd2dec(b[5] & 0x1F);
        year    = bcd2dec(b[6]) + 2000;

        if (month < 1 || month > 12 || day < 1 || day > 31) return false;
        if (hour > 23 || minute > 59 || second > 59) return false;
        return true;
    }

    bool setDateTime(int year, int month, int day,
                     int hour, int minute, int second, int weekday) {
        if (_useM5) {
            m5::rtc_datetime_t dt;
            dt.date.year    = (int16_t)year;
            dt.date.month   = (int8_t)month;
            dt.date.date    = (int8_t)day;
            dt.date.weekDay = (int8_t)(weekday & 7);   // 0=周日
            dt.time.hours   = (int8_t)hour;
            dt.time.minutes = (int8_t)minute;
            dt.time.seconds = (int8_t)second;
            M5.Rtc.setDateTime(&dt);
            return true;
        }

        uint8_t b[7];
        b[0] = dec2bcd(second);
        b[1] = dec2bcd(minute);
        b[2] = dec2bcd(hour);
        b[3] = (uint8_t)(weekday == 0 ? 0x01 : (1u << (weekday & 7)));
        b[4] = dec2bcd(day);
        b[5] = dec2bcd(month);
        b[6] = dec2bcd(year - 2000);
        bool ok = M5.In_I2C.writeRegister(RX8130_ADDR, RX8130_REG_SEC, b, 7, RX8130_FREQ);

        // FLAG(0x1D) 是 write-0-to-clear；写 0x7F 只清 bit7 VLF，
        // 其它标志位写 1 保持不变。CTRL(0x1E) 不是 VLF 寄存器。
        uint8_t flag = 0;
        if (M5.In_I2C.readRegister(RX8130_ADDR, RX8130_REG_FLAG,
                                   &flag, 1, RX8130_FREQ)) {
            if (flag & RX8130_FLAG_VLF) {
                const uint8_t clearVLF = 0x7F;
                M5.In_I2C.writeRegister(RX8130_ADDR, RX8130_REG_FLAG,
                                        &clearVLF, 1, RX8130_FREQ);
            }
        }
        return ok;
    }

    // 从 struct tm 写入 (NTP 同步用)
    bool setDateTime(const struct tm &t) {
        return setDateTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                           t.tm_hour, t.tm_min, t.tm_sec, t.tm_wday);
    }

    String getFormattedTime() {
        int y, mo, d, h, mi, s, wd;
        if (!getDateTime(y, mo, d, h, mi, s, wd)) return String("--:--:--");
        char buf[24];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", y, mo, d, h, mi, s);
        return String(buf);
    }

private:
    bool _useM5    = false;
    bool _forceRaw = false;

    bool validBCD(uint8_t v) { return (v & 0x0F) <= 9 && ((v >> 4) & 0x0F) <= 9; }

    uint8_t bcd2dec(uint8_t v) { return ((v >> 4) * 10) + (v & 0x0F); }

    uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

    // 位编码 -> 0(周日)..6(周六)
    int bitToWeekday(uint8_t bits) {
        if (bits == 0) return 0;
        for (int i = 0; i < 7; i++) {
            if (bits & (1u << i)) return i;
        }
        return 0;
    }
};

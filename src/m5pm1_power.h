#pragma once
// =============================================================
// M5PM1 电源管理封装 (PaperColor 板载 PMIC)
//
// 主路径: M5.Power (M5Unified 已按板卡初始化好 M5PM1)
//   电池电压寄存器: 0x22(VBAT_L, 低字节在前), 单位 mV
//   电量百分比: M5.Power.getBatteryLevel() 内部由电压换算
//   充电状态: M5PM1 没有充电状态寄存器 -> 通常返回 charge_unknown,
//            因此额外用 VBUS 电压判断是否在接 USB
// 备用路径: 直接读 0x22 / 0x24 寄存器
// 地址: 0x6E
// =============================================================

#include <Arduino.h>
#include <M5Unified.h>

#define M5PM1_ADDR       0x6E
#define M5PM1_FREQ       100000
#define M5PM1_REG_PWR_SRC 0x04   // bit0=VIN, bit1=VINOUT, bit2=VBAT节点
#define M5PM1_REG_VBAT_L 0x22    // 电池电压 (mV, 低字节在前)
#define M5PM1_REG_VIN_L  0x24    // VBUS 电压 (mV, 低字节在前)

class M5PM1Power {
public:
    bool begin() {
        return M5.In_I2C.scanID(M5PM1_ADDR, M5PM1_FREQ);
    }

    // 电池电压 (mV), 0 = 读取失败
    uint16_t getBatteryVoltage() {
        int16_t v = M5.Power.getBatteryVoltage();
        if (v > 0) return (uint16_t)v;

        uint8_t b[2] = {0};
        if (M5.In_I2C.readRegister(M5PM1_ADDR, M5PM1_REG_VBAT_L, b, 2, M5PM1_FREQ)) {
            return (uint16_t)(((uint16_t)b[1] << 8) | b[0]);
        }
        return 0;
    }

    // VBUS 电压 (mV), 0 = 未接 USB 或读取失败
    uint16_t getVBUSVoltage() {
        int16_t v = M5.Power.getVBUSVoltage();
        if (v > 0) return (uint16_t)v;

        uint8_t b[2] = {0};
        if (M5.In_I2C.readRegister(M5PM1_ADDR, M5PM1_REG_VIN_L, b, 2, M5PM1_FREQ)) {
            return (uint16_t)(((uint16_t)b[1] << 8) | b[0]);
        }
        return 0;
    }

    // 电量百分比 0~100
    uint8_t getBatteryPercent() {
        int32_t lvl = M5.Power.getBatteryLevel();
        if (lvl >= 0) return (uint8_t)(lvl > 100 ? 100 : lvl);

        uint16_t mv = getBatteryVoltage();
        if (mv == 0) return 0;
        // 与 M5Unified 一致的线性估算: 3300mV -> 0%, 4100mV -> 100%
        int pct = (int)(mv - 3300) * 100 / 800;
        if (pct < 0)   pct = 0;
        if (pct > 100) pct = 100;
        return (uint8_t)pct;
    }

    // -1 未知 / 0 放电 / 1 充电
    int8_t getChargeState() {
        auto st = M5.Power.isCharging();
        if (st == m5::Power_Class::is_charging)    return 1;
        if (st == m5::Power_Class::is_discharging) return 0;
        return -1;   // charge_unknown
    }

    // 电源来源位图
    uint8_t getPowerSource() {
        uint8_t src = 0;
        if (M5.In_I2C.readRegister(M5PM1_ADDR, M5PM1_REG_PWR_SRC, &src, 1, M5PM1_FREQ)) {
            return src & 0x07;
        }
        return 0;
    }
};

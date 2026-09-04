#!/usr/bin/env python3
"""Host-side invariants for the device-status-dashboard MVP."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8")


class DashboardProjectTests(unittest.TestCase):
    def test_platformio_dependencies_are_pinned(self) -> None:
        config = read("platformio.ini")
        for dependency in (
            "espressif32@6.10.0",
            "m5stack/M5Unified @ 0.2.21",
            "m5stack/M5GFX @ 0.2.28",
            "m5stack/M5PM1 @ 1.0.7",
        ):
            self.assertIn(dependency, config)

    def test_official_papercolor_sd_pins_are_used(self) -> None:
        source = read("src/main.cpp")
        for value in ("SD_SCK = 15", "SD_MOSI = 13", "SD_MISO = 14", "SD_CS = 47"):
            self.assertIn(value, source)
        self.assertIn("SD.begin(SD_CS, SPI, 25000000)", source)

    def test_overview_and_details_pages_are_both_present(self) -> None:
        source = read("src/device_status_ui.h")
        self.assertIn("drawOverview(data)", source)
        self.assertIn("drawDetails(data)", source)
        self.assertIn("B Details", source)
        self.assertIn("B Overview", source)

    def test_overview_prioritizes_system_then_storage(self) -> None:
        source = read("src/device_status_ui.h")
        self.assertIn("drawSystemCard(LEFT_X, ROW1_Y, data)", source)
        self.assertIn("drawStorageCard(RIGHT_X, ROW1_Y, data)", source)
        self.assertIn("drawPowerCard(RIGHT_X, ROW2_Y, data)", source)

    def test_dynamic_card_text_is_width_limited_and_footer_keeps_last_refresh(self) -> None:
        source = read("src/device_status_ui.h")
        self.assertIn("String fit(", source)
        self.assertIn("cardText(", source)
        self.assertIn('"Last: %02d:%02d:%02d"', source)
        self.assertIn('"Last: boot +"', source)

    def test_dense_overview_values_use_reduced_scale(self) -> None:
        source = read("src/device_status_ui.h")
        self.assertIn('CARD_W - 28, 0.76f', source)
        self.assertEqual(source.count('top_left, 0.82f'), 2)
        self.assertIn('top_left, 0.72f', source)

    def test_detail_card_text_is_width_limited(self) -> None:
        source = read("src/device_status_ui.h")
        self.assertIn('cardText(name, x + 18, y - 4', source)
        self.assertIn('top_right);', source)
        self.assertIn('"C151 | 4in EPD"', source)
        self.assertIn('"Build " __DATE__', source)

    def test_startup_and_shutdown_chimes_are_present(self) -> None:
        source = read("src/main.cpp")
        self.assertIn('void playChime(bool startup)', source)
        self.assertIn('playChime(true)', source)
        self.assertIn('playChime(false)', source)
        self.assertIn('config.internal_spk = true', source)
        self.assertIn('M5.Speaker.tone(4000, 380)', source)
        self.assertIn('M5.Speaker.tone(8000, 380)', source)
        self.assertIn('M5.Speaker.tone(5000, 380)', source)
        self.assertIn('M5.Speaker.tone(6000, 380)', source)
        self.assertEqual(source.count('M5.delay(500)'), 8)
        self.assertNotIn('M5.Speaker.end()', source)

    def test_m5unified_is_forced_to_the_papercolor_board_profile(self) -> None:
        source = read("src/main.cpp")
        self.assertIn(
            "config.fallback_board = m5::board_t::board_M5PaperColor",
            source,
        )

    def test_sd_scan_is_bounded(self) -> None:
        source = read("src/main.cpp")
        self.assertIn("STORAGE_SCAN_LIMIT = 512", source)
        self.assertIn("count < STORAGE_SCAN_LIMIT", source)

    def test_rtc_is_recovered_after_power_loss(self) -> None:
        source = read("src/main.cpp")
        config = read("platformio.ini")
        self.assertIn("setRTCFromBuildTime", source)
        self.assertIn("ensureRTCTime", source)
        self.assertIn("rtc.isVoltageLow()", source)
        self.assertIn("rtc.setForceRaw(true)", source)
        self.assertIn("pre:build_time.py", config)

    def test_rtc_build_time_is_calibrated_once_per_firmware_build(self) -> None:
        source = read("src/main.cpp")
        self.assertIn("RTC_BUILD_LAG_SEC = 16", source)
        self.assertIn("Preferences", source)
        self.assertIn('getULong("rtc_build", 0)', source)
        self.assertIn('putULong("rtc_build", BUILD_UNIX_TIME)', source)


if __name__ == "__main__":
    unittest.main()

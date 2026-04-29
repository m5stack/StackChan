/*
 * SPDX-FileCopyrightText: 2026 GOROman / null-bot
 *
 * SPDX-License-Identifier: MIT
 *
 * wake_manager.h
 *
 * Deep sleep / wake management for StackChan (M5Stack CoreS3).
 *
 * Wake source path:
 *   Si12T pin20 IRQ -> FPC1 pin7 -> CoreS3 CTP1 pin5 -> AW9523B(0x58) P1_2
 *   -> INTN -> ESP32-S3 GPIO21 (RTC IO) -> ext0 wake.
 */

#pragma once

#include <stdint.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** GPIO that AW9523B's INTN line is wired to on the CoreS3. */
#define WAKE_MANAGER_AW9523_INT_GPIO GPIO_NUM_21

typedef enum {
    WAKE_REASON_UNKNOWN = 0,
    WAKE_REASON_POWER_ON,         // not from deep sleep
    WAKE_REASON_HEAD_TOUCH_SI12T,  // AW9523 P1_2 -> Si12T
    WAKE_REASON_FT6336_OR_OTHER,   // some other AW9523 line
    WAKE_REASON_OTHER_EXT0,
} wake_reason_t;

/**
 * @brief Inspect ESP32-S3 wake cause + AW9523 INT_STATUS to determine why we
 *        booted. Should be called once early during boot, before AW9523 input
 *        registers get read by other code (the read clears the latch).
 */
wake_reason_t wake_manager_check_boot_reason(void);

/**
 * @brief Enter ESP32-S3 deep sleep with ext0 wakeup armed on GPIO21 (low).
 *
 * Caller is expected to have already:
 *   - Saved any persistent state.
 *   - Turned the backlight / amplifier / camera off.
 *   - Left the I2C bus in a state where Si12T continues to assert INT on
 *     touch (see si12t_enable_irq_level_active()).
 *   - Configured AW9523 P1_2 as the only unmasked interrupt source
 *     (see Aw9523::EnableTouchIrqWakeSource()).
 *
 * This call does not return.
 */
void wake_manager_enter_deep_sleep(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

namespace view {

/**
 * @brief Full screen overlay for reefwatch camera frames, on LVGL's top layer so it shows over
 * any app and in AI agent mode. Tap a detection to watch that camera live, tap again to stop.
 *
 */
void reefwatch_view_init();

}  // namespace view

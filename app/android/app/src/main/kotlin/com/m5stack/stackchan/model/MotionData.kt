/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package com.m5stack.stackchan.model

data class MotionData(
    var type: String = "bleMotion",
    var pitchServo: MotionDataItem,
    var yawServo: MotionDataItem,
)

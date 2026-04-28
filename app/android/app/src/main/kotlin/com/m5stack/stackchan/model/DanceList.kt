/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package com.m5stack.stackchan.model

data class DanceList(
    var danceData: MutableList<DanceData>?,
    var danceIndex: Int?,
    var danceName: String?
)
package com.m5stack.stackchan.model

data class DanceData(
    var leftEye: ExpressionItem,
    var rightEye: ExpressionItem,
    var mouth: ExpressionItem,
    var yawServo: MotionDataItem,
    var pitchServo: MotionDataItem,
    var leftRgbColor: String = "#00000000",
    var rightRgbColor: String = "#00000000",
    var durationMs: Int = 1000
)
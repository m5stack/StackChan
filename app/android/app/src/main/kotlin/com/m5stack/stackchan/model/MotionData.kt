package com.m5stack.stackchan.model

data class MotionData(
    var type: String = "bleMotion",
    var pitchServo: MotionDataItem,
    var yawServo: MotionDataItem,
)

package com.m5stack.stackchan.model

data class ExpressionData(
    var type: String = "bleAvatar",
    var leftEye: ExpressionItem,
    var rightEye: ExpressionItem,
    var mouth: ExpressionItem
)
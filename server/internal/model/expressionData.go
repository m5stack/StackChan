/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package model

type ExpressionData struct {
	Type     string         `json:"type"`
	LeftEye  ExpressionItem `json:"leftEye"`
	RightEye ExpressionItem `json:"rightEye"`
	Mouth    ExpressionItem `json:"mouth"`
}

type ExpressionItem struct {
	X        int `json:"x"`
	Y        int `json:"y"`
	Rotation int `json:"rotation"`
	Weight   int `json:"weight"`
	Size     int `json:"size"`
}

type MotionData struct {
	Type       string         `json:"type"`
	PitchServo MotionDataItem `json:"pitchServo"`
	YawServo   MotionDataItem `json:"yawServo"`
}

type MotionDataItem struct {
	Angle  int `json:"angle"`
	Speed  int `json:"speed"`
	Rotate int `json:"rotate"`
}

type DanceData struct {
	LeftEye    ExpressionItem `json:"leftEye"`
	RightEye   ExpressionItem `json:"rightEye"`
	Mouth      ExpressionItem `json:"mouth"`
	PitchServo MotionDataItem `json:"pitchServo"`
	YawServo   MotionDataItem `json:"yawServo"`
	DurationMs int            `json:"durationMs"`
}

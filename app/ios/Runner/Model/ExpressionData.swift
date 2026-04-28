/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import Foundation

struct ExpressionData : Codable {
    var type: String = "bleAvatar"
    var leftEye: ExpressionItem
    var rightEye: ExpressionItem
    var mouth: ExpressionItem
}

struct ExpressionItem : Codable {
    var x: Int = 0
    var y: Int = 0
    var rotation: Int = 0
    var weight: Int = 0
    var size: Int = 0
    
    func copy() -> ExpressionItem {
        ExpressionItem(
            x: self.x,
            y: self.y,
            rotation: self.rotation,
            weight: self.weight,
            size: self.size
        )
    }
}

struct MotionData : Codable {
    var type: String = "bleMotion"
    var pitchServo: MotionDataItem
    var yawServo: MotionDataItem
    
    func toJsonString() -> String {
        let encoder = JSONEncoder()
        if let jsonData = try? encoder.encode(self),
           let jsonString = String(data: jsonData, encoding: .utf8) {
            return jsonString
        }
        return "{}"
    }
}

struct MotionDataItem: Codable {
    var angle: Int = 0
    var speed: Int = 500
    var rotate: Int = 0
    
    init() {
        self.angle = 0
        self.speed = 500
        self.rotate = 0
    }
    
    init(angle: Int, speed: Int = 500) {
        self.angle = angle
        self.speed = speed
        self.rotate = 0
    }
    
    enum CodingKeys: String, CodingKey {
        case angle
        case speed
        case rotate
    }
    
    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        
        if angle != 0 {
            try container.encode(angle, forKey: .angle)
            try container.encode(speed, forKey: .speed)
        } else if rotate != 0 {
            try container.encode(rotate, forKey: .rotate)
            try container.encode(speed, forKey: .speed)
        } else {
            try container.encode(angle, forKey: .angle)
            try container.encode(speed, forKey: .speed)
        }
    }
    
    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        self.angle = try container.decodeIfPresent(Int.self, forKey: .angle) ?? 0
        self.speed = try container.decodeIfPresent(Int.self, forKey: .speed) ?? 500
        self.rotate = try container.decodeIfPresent(Int.self, forKey: .rotate) ?? 0
    }
    
    func copy() -> MotionDataItem {
        MotionDataItem(
            angle: self.angle,
            speed: self.speed,
        )
    }
}

//
//  Dance.swift
//  StackChan
//
//  Created by 袁智鸿 on 2026/1/16.
//

import Foundation

struct DanceList: Codable, Identifiable {
    var danceData: [DanceData]?
    var danceIndex: Int?
    var danceName: String?
    
    var id: String = UUID().uuidString
    
    enum CodingKeys: String, CodingKey {
        case danceData
        case danceIndex
        case danceName
    }
}

struct DanceData : Codable,Identifiable {
    var leftEye: ExpressionItem // Left eye, default weight = 100
    var rightEye: ExpressionItem // Right eye, default weight = 100
    var mouth: ExpressionItem // Mouth, default weight = 0
    var yawServo: MotionDataItem // Yaw rotation, angle range (-1280 ~ 1280), default 0
    var pitchServo: MotionDataItem  // Pitch movement, angle range (0 ~ 900), default 0
    
    var leftRgbColor: String = "#00000000"
    var rightRgbColor: String = "#00000000"
    
    var durationMs: Int // Duration in milliseconds, default 1000
    var id: String = UUID().uuidString
    
    enum CodingKeys: String, CodingKey {
        case leftEye, rightEye, mouth, yawServo, pitchServo, leftRgbColor, rightRgbColor, durationMs
    }
    
    static func from(jsonString: String) -> DanceData? {
        guard !jsonString.isEmpty else {
            print("JSON string is empty")
            return nil
        }
        guard let jsonData = jsonString.data(using: .utf8) else {
            print("Failed to convert string to UTF-8 data")
            return nil
        }
        
        do {
            let decoder = JSONDecoder()
            let danceData = try decoder.decode(DanceData.self, from: jsonData)
            return danceData
        } catch {
            return nil
        }
    }
    
    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        leftEye = try container.decode(ExpressionItem.self, forKey: .leftEye)
        rightEye = try container.decode(ExpressionItem.self, forKey: .rightEye)
        mouth = try container.decode(ExpressionItem.self, forKey: .mouth)
        yawServo = try container.decode(MotionDataItem.self, forKey: .yawServo)
        pitchServo = try container.decode(MotionDataItem.self, forKey: .pitchServo)
        leftRgbColor = try container.decodeIfPresent(String.self, forKey: .leftRgbColor) ?? "#00000000"
        rightRgbColor = try container.decodeIfPresent(String.self, forKey: .rightRgbColor) ?? "#00000000"
        durationMs = try container.decode(Int.self, forKey: .durationMs)
        id = UUID().uuidString
    }
    
    init(
        leftEye: ExpressionItem,
        rightEye: ExpressionItem,
        mouth: ExpressionItem,
        yawServo: MotionDataItem,
        pitchServo: MotionDataItem,
        leftRgbColor: String = "#00000000",
        rightRgbColor: String = "#00000000",
        durationMs: Int,
        id: String = UUID().uuidString
    ) {
        self.leftEye = leftEye
        self.rightEye = rightEye
        self.mouth = mouth
        self.yawServo = yawServo
        self.pitchServo = pitchServo
        self.leftRgbColor = leftRgbColor
        self.rightRgbColor = rightRgbColor
        self.durationMs = durationMs
        self.id = id
    }
    
    func copy() -> DanceData {
        DanceData(
            leftEye: self.leftEye.copy(),
            rightEye: self.rightEye.copy(),
            mouth: self.mouth.copy(),
            yawServo: self.yawServo.copy(),
            pitchServo: self.pitchServo.copy(),
            leftRgbColor: self.leftRgbColor,
            rightRgbColor: self.rightRgbColor,
            durationMs: self.durationMs,
            id: UUID().uuidString
        )
    }
}

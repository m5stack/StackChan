/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import Foundation

enum MsgType: UInt8, Codable {
    case opus = 0x01
    case jpeg = 0x02
    case controlAvatar = 0x03
    case controlMotion = 0x04
    
    case onCamera = 0x05
    case offCamera = 0x06
    
    case textMessage = 0x07
    case requestCall = 0x09
    case refuseCall = 0x0A
    case agreeCall = 0x0B
    case hangupCall = 0x0C
    
    case updateDeviceName = 0x0D
    case getDeviceName = 0x0E
    
    case ping = 0x10
    case pong = 0x11
    
    case onPhoneScreen = 0x12
    case offPhoneScreen = 0x13
    
    case dance = 0x14
    
    case getAvatarPosture = 0x15
    
    case deviceOffline = 0x16
    case deviceOnline = 0x17
}

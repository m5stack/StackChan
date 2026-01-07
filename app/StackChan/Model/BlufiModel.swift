/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import Foundation

struct BlufiModel<T:Codable>: Codable {
    
    var cmd: String? = nil
    var data: T? = nil
    
    func toJson() -> String? {
        let encoder = JSONEncoder()
        encoder.outputFormatting = .prettyPrinted
        guard let jsonData = try? encoder.encode(self) else { return nil }
        return String(data: jsonData, encoding: .utf8)
    }
    
    static func fromJson(_ json: String) -> BlufiModel<T>? {
        guard let jsonData = json.data(using: .utf8) else { return nil }
        let decoder = JSONDecoder()
        return try? decoder.decode(BlufiModel<T>.self, from: jsonData)
    }
}

struct BlufiWifi : Codable {
    var ssid: String?
    var password: String?
    
    func toJson() -> String? {
        let encoder = JSONEncoder()
        encoder.outputFormatting = .prettyPrinted
        guard let jsonData = try? encoder.encode(self) else { return nil }
        return String(data: jsonData, encoding: .utf8)
    }
    
    static func fromJson(_ json: String) -> BlufiWifi? {
        guard let jsonData = json.data(using: .utf8) else { return nil }
        let decoder = JSONDecoder()
        return try? decoder.decode(BlufiWifi.self, from: jsonData)
    }
}

struct BlufiNotifyState : Codable {
    var type: Int?
    var state: String?
    
    func toJson() -> String? {
        let encoder = JSONEncoder()
        encoder.outputFormatting = .prettyPrinted
        guard let jsonData = try? encoder.encode(self) else { return nil }
        return String(data: jsonData, encoding: .utf8)
    }
    
    static func fromJson(_ json: String) -> BlufiNotifyState? {
        guard let jsonData = json.data(using: .utf8) else { return nil }
        let decoder = JSONDecoder()
        return try? decoder.decode(BlufiNotifyState.self, from: jsonData)
    }
}

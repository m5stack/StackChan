/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import Foundation

struct Response<T: Codable>: Codable {
    let code: Int?
    let message: String?
    let data: T?
    
    var isSuccess: Bool {
        return code == 0
    }
    
    func unwrap(or defaultValue: T) -> T {
        return data ?? defaultValue
    }
    
    static func decode(from jsonData: Data) throws -> Response<T> {
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
        do {
            return try decoder.decode(Response<T>.self, from: jsonData)
        } catch let DecodingError.dataCorrupted(context) {
            print("🔴 Data corrupted: \(context.debugDescription)")
            printCodingPath(context.codingPath)
            printJSON(jsonData)
            throw DecodingError.dataCorrupted(context)
        } catch let DecodingError.keyNotFound(key, context) {
            print("🔴 Key '\(key.stringValue)' not found: \(context.debugDescription)")
            printCodingPath(context.codingPath)
            printJSON(jsonData)
            throw DecodingError.keyNotFound(key, context)
        } catch let DecodingError.typeMismatch(type, context) {
            print("🔴 Type '\(type)' mismatch: \(context.debugDescription)")
            printCodingPath(context.codingPath)
            printJSON(jsonData)
            throw DecodingError.typeMismatch(type, context)
        } catch let DecodingError.valueNotFound(value, context) {
            print("🔴 Value '\(value)' not found: \(context.debugDescription)")
            printCodingPath(context.codingPath)
            printJSON(jsonData)
            throw DecodingError.valueNotFound(value, context)
        } catch {
            print("🔴 Other errors in the analysis: \(error)")
            printJSON(jsonData)
            throw error
        }
    }
    
    static func decode(from json: [String: Any]) throws -> Response<T> {
        let data = try JSONSerialization.data(withJSONObject: json, options: [])
        return try decode(from: data)
    }
    
    func debugDescription() -> String {
        return "Response(code: \(code ?? 0), message: \(message ?? ""), data: \(String(describing: data)))"
    }
}

fileprivate func printCodingPath(_ codingPath: [CodingKey]) {
    let path = codingPath.map { $0.stringValue }.joined(separator: ".")
    print("📍 Error path: \(path)")
}

fileprivate func printJSON(_ data: Data) {
    if let obj = try? JSONSerialization.jsonObject(with: data, options: []),
       let prettyData = try? JSONSerialization.data(withJSONObject: obj, options: [.prettyPrinted]),
       let str = String(data: prettyData, encoding: .utf8) {
        print("📄 Original JSON:\n\(str)")
    } else if let str = String(data: data, encoding: .utf8) {
        print("📄 Original JSON:\n\(str)")
    } else {
        print("⚠️ Unable to parse the original JSON")
    }
}

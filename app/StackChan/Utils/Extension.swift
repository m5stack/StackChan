/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI
import simd

extension UIApplication {
    func endEditing() {
        sendAction(#selector(UIResponder.resignFirstResponder),
                   to: nil, from: nil, for: nil)
    }
}

extension Color {
    init?(hex: String) {
        var hexString = hex.trimmingCharacters(in: .whitespacesAndNewlines).uppercased()
        if hexString.hasPrefix("#") {
            hexString.removeFirst()
        }
        guard hexString.count == 6 || hexString.count == 8 else { return nil }
        
        var rgbValue: UInt64 = 0
        Scanner(string: hexString).scanHexInt64(&rgbValue)
        
        let r, g, b, a: Double
        if hexString.count == 6 {
            r = Double((rgbValue & 0xFF0000) >> 16) / 255
            g = Double((rgbValue & 0x00FF00) >> 8) / 255
            b = Double(rgbValue & 0x0000FF) / 255
            a = 1.0
        } else {
            a = Double((rgbValue & 0xFF000000) >> 24) / 255
            r = Double((rgbValue & 0x00FF0000) >> 16) / 255
            g = Double((rgbValue & 0x0000FF00) >> 8) / 255
            b = Double(rgbValue & 0x000000FF) / 255
        }
        self.init(red: r, green: g, blue: b, opacity: a)
    }
    
    func toHex() -> String? {
        let uiColor = UIColor(self)
        var r: CGFloat = 0
        var g: CGFloat = 0
        var b: CGFloat = 0
        var a: CGFloat = 0
        guard uiColor.getRed(&r, green: &g, blue: &b, alpha: &a) else { return nil }
        return String(format: "#%02X%02X%02X",
                      Int(r * 255),
                      Int(g * 255),
                      Int(b * 255))
    }
}

extension UIImage {
    func scaledToFill(_ targetSize: CGSize) -> UIImage {
        let renderer = UIGraphicsImageRenderer(size: targetSize)
        return renderer.image { _ in
            self.draw(in: CGRect(origin: .zero, size: targetSize))
        }
    }
    /// Optional image compression method
    /// - Parameters:
    ///   - resolutionSize: Target resolution (optional). If nil, no cropping or scaling is applied
    ///   - memorySize: Target memory size in MB (optional). If nil, no memory compression is applied
    ///   - cropCenter: Whether to crop from center. Default false = aspect-fit scaling
    func compress(to resolutionSize: CGSize? = nil, memorySize: Float? = nil, cropCenter: Bool = false) -> Data? {
        var scaledImage = self
        
        // 1. Crop or scale based on target resolution
        if let resolutionSize = resolutionSize {
            if cropCenter {
                // 1. Calculate scale to ensure the image fully covers the target resolution
                let scale = max(resolutionSize.width / size.width, resolutionSize.height / size.height)
                let scaledSize = CGSize(width: size.width * scale, height: size.height * scale)
                
                // 2. Calculate offset to ensure center cropping
                let originX = (scaledSize.width - resolutionSize.width) / 2
                let originY = (scaledSize.height - resolutionSize.height) / 2
                
                // 3. Start drawing
                UIGraphicsBeginImageContextWithOptions(resolutionSize, false, 1.0)
                self.draw(in: CGRect(x: -originX, y: -originY, width: scaledSize.width, height: scaledSize.height))
                scaledImage = UIGraphicsGetImageFromCurrentImageContext() ?? self
                UIGraphicsEndImageContext()
            } else {
                // Aspect-fit scaling
                let scale = min(resolutionSize.width / size.width, resolutionSize.height / size.height)
                let newSize = CGSize(width: size.width * scale, height: size.height * scale)
                UIGraphicsBeginImageContextWithOptions(resolutionSize, false, 1.0)
                self.draw(in: CGRect(origin: .zero, size: newSize))
                scaledImage = UIGraphicsGetImageFromCurrentImageContext() ?? self
                UIGraphicsEndImageContext()
            }
        }
        
        // 2. Compress based on target memory size
        guard let memorySize = memorySize else {
            return scaledImage.jpegData(compressionQuality: 1)
        }
        
        let maxBytes = Int(memorySize * 1024 * 1024) // MB -> Bytes
        var compression: CGFloat = 1.0
        var imageData = scaledImage.jpegData(compressionQuality: compression)
        
        // Keep compressing until size requirement is met or compression limit is reached
        while let data = imageData, data.count > maxBytes, compression > 0.01 {
            compression *= 0.7
            imageData = scaledImage.jpegData(compressionQuality: compression)
        }
        
        return imageData
    }
    /// Compress image only based on target memory size, keeping resolution and aspect ratio unchanged
    /// - Parameter memorySize: Target memory size in MB
    /// - Returns: Compressed JPEG data
    func compress(toMemorySize memorySize: Float) -> Data? {
        let maxBytes = Int(memorySize * 1024 * 1024)
        var compression: CGFloat = 1.0
        var imageData = self.jpegData(compressionQuality: compression)
        
        while let data = imageData, data.count > maxBytes, compression > 0.01 {
            compression *= 0.7
            imageData = self.jpegData(compressionQuality: compression)
        }
        return imageData
    }
}

extension String {
    func jsonPrint() {
        guard let data = self.data(using: .utf8) else {
            print(self)
            return
        }
        do {
            let jsonObject = try JSONSerialization.jsonObject(with: data, options: [])
            let prettyData = try JSONSerialization.data(withJSONObject: jsonObject, options: [.prettyPrinted, .sortedKeys])
            if let prettyString = String(data: prettyData, encoding: .utf8) {
                print("✅ JSON formatted output:\n\(prettyString)")
            } else {
                print(self)
            }
        } catch {
            print("❌ Invalid JSON format: \(error.localizedDescription)")
            print(self)
        }
    }
    
    func leftPadding(toLength: Int, withPad character: Character) -> String {
        if count < toLength {
            return String(repeatElement(character, count: toLength - count)) + self
        } else {
            return self
        }
    }
    
    /// Convert a MAC address string into 6-byte Data
    func macToData() -> Data? {
        // Remove separators such as ":" or "-"
        let cleaned = self.replacingOccurrences(of: "[:\\-]", with: "", options: .regularExpression)
        
        // Must be exactly 12 hexadecimal characters
        guard cleaned.count == 12 else { return nil }
        
        var data = Data()
        var index = cleaned.startIndex
        for _ in 0..<6 {
            let nextIndex = cleaned.index(index, offsetBy: 2)
            let byteString = String(cleaned[index..<nextIndex])
            guard let byte = UInt8(byteString, radix: 16) else { return nil }
            data.append(byte)
            index = nextIndex
        }
        return data
    }
    
    func toData() -> Data? {
        return self.data(using: .utf8)
    }
    
}

extension Encodable {
    func toDictionary() -> [String: Any]? {
        guard let data = try? JSONEncoder().encode(self),
              let dict = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return nil
        }
        return dict
    }
    
    func toListDictionary() -> [[String: Any]]? {
        if let singleDict = self.toDictionary() {
            return [singleDict]
        }
        if let arrayData = try? JSONEncoder().encode(self),
           let jsonArray = try? JSONSerialization.jsonObject(with: arrayData) as? [[String: Any]] {
            return jsonArray
        }
        
        return nil
    }
    
    func toJsonString(prettyPrinted: Bool = false) -> String {
        let encoder = JSONEncoder()
        if prettyPrinted {
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        }
        do {
            let data = try encoder.encode(self)
            return String(data: data, encoding: .utf8) ?? "{}"
        } catch {
            print("❌ JSON serialization failed: \(error)")
            return "{}"
        }
    }
    
    func toData() -> Data? {
        let encoder = JSONEncoder()
        do {
            return try encoder.encode(self)
        } catch {
            print("❌ Failed to convert JSON to Data: \(error.localizedDescription)")
            return nil
        }
    }
}

extension CGRect {
    var minDimension: CGFloat {
        min(width, height)
    }
}


extension View {
    
    @ViewBuilder
    func rippleDiffusion() -> some View {
        TimelineView(.animation) { timeline in
            // Calculate time interval
            let time = timeline.date.timeIntervalSinceReferenceDate
            ZStack {
                // Multiple ripple cycles
                ForEach(0..<3) { i in
                    let progress = (time + Double(i) * 0.5).truncatingRemainder(dividingBy: 1.5) / 1.5
                    Circle()
                        .stroke(Color.blue.opacity(1 - progress), lineWidth: 2)
                        .scaleEffect(0.5 + progress * 2)
                }
            }
            .drawingGroup()
        }
    }
    
    @ViewBuilder
    func glassEffectCircle() -> some View {
        if #available(iOS 26.0, *) {
            self.glassEffect()
        } else {
            self.background(
                Circle()
                    .fill(.ultraThinMaterial)
            )
        }
    }
    
    @ViewBuilder
    func glassEffectRegular(cornerRadius : CGFloat) -> some View {
        if #available(iOS 26.0, *) {
            self.glassEffect(.regular,in: RoundedRectangle(cornerRadius: cornerRadius))
        } else {
            self
                .background(
                    RoundedRectangle(cornerRadius: cornerRadius)
                        .fill(.thinMaterial)
                )
        }
    }
    
    @ViewBuilder
    func presentationBackgroundClear() -> some View {
        if #available(iOS 26.0, *) {
            self.presentationBackground(.clear)
        } else {
            self.presentationBackground(.ultraThinMaterial)
        }
    }
    
    @ViewBuilder
    func glassButtonStyle() -> some View {
        if #available(iOS 26.0, *) {
            self.buttonStyle(.glass)
        } else {
            self.buttonStyle(.bordered)
        }
    }
    
    func hideKeyboard() {
        UIApplication.shared.sendAction(#selector(UIResponder.resignFirstResponder), to: nil, from: nil, for: nil)
    }
}

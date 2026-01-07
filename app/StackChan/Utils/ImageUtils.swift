/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import UIKit

final class ImageUtils {
    
    // MARK: - Singleton
    static let shared = ImageUtils()
    private init() {}
    
    // MARK: - Public Methods

    /// Resize the image to the specified resolution and export it /// - Parameters:
    ///   - image: Input the original image
    ///   - targetSize: Target resolution (e.g. CGSize(width: 1080, height: 1920))
    ///   - format: Export format (default JPEG)
    ///   - quality: JPEG compression quality (0-1, default 1)
    /// - Returns: Converted Data (JPEG or PNG), nil if failure
    func exportScaledImageData(
        from image: UIImage,
        targetSize: CGSize,
        format: ImageFormat = .jpeg,
        quality: CGFloat = 1.0
    ) -> Data? {
        
        // Use UIGraphicsImageRenderer for high-quality scaling
        let renderer = UIGraphicsImageRenderer(size: targetSize)
        let scaledImage = renderer.image { _ in
            image.draw(in: CGRect(origin: .zero, size: targetSize))
        }
        
        switch format {
        case .jpeg:
            return scaledImage.jpegData(compressionQuality: quality)
        case .png:
            return scaledImage.pngData()
        }
    }
    
    // MARK: - Image Format Enum
    enum ImageFormat {
        case jpeg
        case png
    }
}

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import Foundation
import CryptoKit

class FileUtils {
    
    static let shared = FileUtils()
    private init() {}
    
    func cacheDirectory() -> URL {
        let paths = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)
        return paths[0]
    }
    
    func hashedKey(for url: String) -> String {
        let data = Data(url.utf8)
        let hash = SHA256.hash(data: data)
        return hash.compactMap { String(format: "%02x", $0) }.joined()
    }
    
    //File download, with built-in cache. First, check if the file exists locally. If it does, directly return the path; if not, download the file and then return the path.
    func download(url: String) async throws -> String {
        let key = hashedKey(for: url)
        let cacheURL = cacheDirectory().appendingPathComponent(key)
        
        if FileManager.default.fileExists(atPath: cacheURL.path) {
            return cacheURL.path
        }
        
        guard let requestURL = URL(string: url) else {
            throw URLError(.badURL)
        }
        
        let (data, _) = try await URLSession.shared.data(from: requestURL)
        try data.write(to: cacheURL)
        return cacheURL.path
    }
}

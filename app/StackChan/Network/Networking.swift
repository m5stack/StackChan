/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import Foundation

class Networking {
    
    static let shared = Networking()
    private init() {}
    
    enum HTTPMethod: String {
        case GET,POST,PUT,DELETE
    }
    
    private func request(
        urlString: String,
        method: HTTPMethod,
        parameters: Any? = nil,
        headers: [String: String] = [:],
        completion: @escaping (Result<Data, Error>) -> Void
    ) {
        var finalURLString = urlString
        var httpBody: Data? = nil
        
        if method == .GET {
            if let params = parameters as? [String: Any], !params.isEmpty {
                var components = URLComponents(string: urlString)
                components?.queryItems = params.map { URLQueryItem(name: $0.key, value: "\($0.value)") }
                if let urlWithQuery = components?.url?.absoluteString {
                    finalURLString = urlWithQuery
                }
            }
        } else {
            if let params = parameters {
                requestSetContentType: do {
                    requestSetBody: do {
                        do {
                            if let dict = params as? [String: Any] {
                                httpBody = try JSONSerialization.data(withJSONObject: dict, options: [])
                            } else if let array = params as? [Any] {
                                httpBody = try JSONSerialization.data(withJSONObject: array, options: [])
                            } else {
                                httpBody = try JSONSerialization.data(withJSONObject: params, options: [])
                            }
                        } catch {
                            completion(.failure(error))
                            return
                        }
                    }
                }
            }
        }
        
        guard let url = URL(string: finalURLString) else {
            completion(.failure(NSError(domain: "Invalid URL", code: -1)))
            return
        }
        
        var request = URLRequest(url: url)
        request.httpMethod = method.rawValue
        
        if method != .GET, httpBody != nil {
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
            request.httpBody = httpBody
        }
        
        setHandler(request: &request, headers: headers)
        logRequest(request)
        
        URLSession.shared.dataTask(with: request) { data, response, error in
            DispatchQueue.main.async {
                if let error = error {
                    completion(.failure(error))
                    return
                }
                guard let data = data else {
                    completion(.failure(NSError(domain: "No data returned", code: -2)))
                    return
                }
                self.logResponse(data: data)
                completion(.success(data))
            }
        }.resume()
    }
    
    func get(pathUrl: String, parameters: [String: Any] = [:], headers: [String: String] = [:], baseUrlString: String? = nil, completion: @escaping (Result<Data, Error>) -> Void) {
        let finalUrl = (baseUrlString ?? Urls.getBaseUrl()) + pathUrl
        request(urlString: finalUrl, method: .GET, parameters: parameters, headers: headers, completion: completion)
    }
    
    func post(pathUrl: String, parameters: Any? = nil, headers: [String: String] = [:], baseUrlString: String? = nil, completion: @escaping (Result<Data, Error>) -> Void) {
        let finalUrl = (baseUrlString ?? Urls.getBaseUrl()) + pathUrl
        request(urlString: finalUrl, method: .POST, parameters: parameters, headers: headers, completion: completion)
    }
    
    private func setHandler(request: inout URLRequest, headers: [String: String]) {
        for (key, value) in headers {
            request.setValue(value, forHTTPHeaderField: key)
        }
        if let token = UserDefaults.standard.string(forKey: ValueConstant.token), !token.isEmpty {
            request.setValue(token, forHTTPHeaderField: ValueConstant.Authorization)
        }
    }
    
    func postFromData(pathUrl: String,
                      parameters: [String: Any?] = [:],
                      headers: [String: String] = [:],
                      baseUrlString: String? = nil,
                      suffix:String? = nil,
                      completion: @escaping (Result<Data, Error>) -> Void) {
        let finalUrl = (baseUrlString ?? Urls.getBaseUrl()) + pathUrl
        guard let url = URL(string: finalUrl) else {
            completion(.failure(NSError(domain: "Invalid URL", code: -1)))
            return
        }
        
        var request = URLRequest(url: url)
        request.httpMethod = HTTPMethod.POST.rawValue
        
        let boundary = "Boundary-\(UUID().uuidString)"
        request.setValue("multipart/form-data; boundary=\(boundary)", forHTTPHeaderField: "Content-Type")
        
        setHandler(request: &request, headers: headers)
        
        var requestBody = Data()
        
        for (key, value) in parameters {
            if let value = value {
                if let fileData = value as? Data {
                    let type = mimeType(for: fileData)
                    let fileName = UUID().uuidString + (suffix ?? "")
                    requestBody.append("--\(boundary)\r\n".data(using: .utf8)!)
                    requestBody.append("Content-Disposition: form-data; name=\"\(key)\"; filename=\"\(fileName)\"\r\n".data(using: .utf8)!)
                    requestBody.append("Content-Type: \(type)\r\n\r\n".data(using: .utf8)!)
                    requestBody.append(fileData)
                    requestBody.append("\r\n".data(using: .utf8)!)
                } else if let array = value as? [Any] {
                    if let jsonData = try? JSONSerialization.data(withJSONObject: array, options: []) {
                        let jsonString = String(data: jsonData, encoding: .utf8)
                        requestBody.append("--\(boundary)\r\n".data(using: .utf8)!)
                        requestBody.append("Content-Disposition: form-data; name=\"\(key)\"\r\n\r\n".data(using: .utf8)!)
                        requestBody.append("\(jsonString ?? "[]")\r\n".data(using: .utf8)!)
                    }
                } else if let dict = value as? [String:Any] {
                    if let jsonData = try? JSONSerialization.data(withJSONObject: dict, options: []) {
                        let jsonString = String(data: jsonData, encoding: .utf8)
                        requestBody.append("--\(boundary)\r\n".data(using: .utf8)!)
                        requestBody.append("Content-Disposition: form-data; name=\"\(key)\"\r\n\r\n".data(using: .utf8)!)
                        requestBody.append("\(jsonString ?? "{}")\r\n".data(using: .utf8)!)
                    }
                } else {
                    let str = "\(value)"
                    requestBody.append("--\(boundary)\r\n".data(using: .utf8)!)
                    requestBody.append("Content-Disposition: form-data; name=\"\(key)\"\r\n\r\n".data(using: .utf8)!)
                    requestBody.append("\(str)\r\n".data(using: .utf8)!)
                }
            }
        }
        requestBody.append("--\(boundary)--\r\n".data(using: .utf8)!)
        request.httpBody = requestBody
        
        logRequest(request)
        
        URLSession.shared.dataTask(with: request) { data, response, error in
            DispatchQueue.main.async {
                if let error = error {
                    completion(.failure(error))
                    return
                }
                guard let data = data else {
                    completion(.failure(NSError(domain: "No data returned", code: -2)))
                    return
                }
                self.logResponse(data: data)
                completion(.success(data))
            }
        }.resume()
    }
    
    func anyToJson(data: Any) -> String {
        func convert(_ value: Any) -> Any {
            if let dict = value as? [String: Any] {
                var newDict: [String: Any] = [:]
                for (k, v) in dict {
                    newDict[k] = convert(v)
                }
                return newDict
            } else if let dict = value as? [String: Any?] {
                var newDict: [String: Any] = [:]
                for (k, v) in dict {
                    if let unwrapped = v {
                        newDict[k] = convert(unwrapped)
                    } else {
                        newDict[k] = NSNull()
                    }
                }
                return newDict
            } else if let array = value as? [Any] {
                return array.map { convert($0) }
            } else if let array = value as? [Any?] {
                return array.map { $0 == nil ? NSNull() : convert($0!) }
            } else if value is Int || value is Double || value is Bool || value is String {
                return value
            } else {
                return "\(value)"
            }
        }
        
        let converted = convert(data)
        
        if JSONSerialization.isValidJSONObject(converted) {
            do {
                let jsonData = try JSONSerialization.data(withJSONObject: converted, options: [])
                return String(data: jsonData, encoding: .utf8) ?? "[]"
            } catch {
                print("JSON Serialization error: \(error)")
                return "[]"
            }
        } else {
            if let str = converted as? String {
                return "\"\(str)\""
            } else {
                return "\(converted)"
            }
        }
    }
    
    func put(pathUrl: String, parameters: Any? = nil, headers: [String: String] = [:], baseUrlString: String? = nil, completion: @escaping (Result<Data, Error>) -> Void) {
        let finalUrl = (baseUrlString ?? Urls.getBaseUrl()) + pathUrl
        request(urlString: finalUrl, method: .PUT, parameters: parameters, headers: headers, completion: completion)
    }
    
    func delete(pathUrl: String, parameters: Any? = nil, headers: [String: String] = [:], baseUrlString: String? = nil, completion: @escaping (Result<Data, Error>) -> Void) {
        let finalUrl = (baseUrlString ?? Urls.getBaseUrl()) + pathUrl
        request(urlString: finalUrl, method: .DELETE, parameters: parameters, headers: headers, completion: completion)
    }
    
    func download(pathUrl: String,
                  parameters: [String: Any] = [:],
                  headers: [String: String] = [:],
                  baseUrlString: String? = nil,
                  completion: @escaping (Result<String, Error>) -> Void) {
        
        let finalUrl = (baseUrlString ?? Urls.getBaseUrl()) + pathUrl
        let key = FileUtils.shared.hashedKey(for: finalUrl)
        let cacheURL = FileUtils.shared.cacheDirectory().appendingPathComponent(key)
        
        if FileManager.default.fileExists(atPath: cacheURL.path) {
            completion(.success(cacheURL.path))
            return
        }
        
        var request = URLRequest(url: URL(string: finalUrl)!)
        request.httpMethod = "GET"
        
        setHandler(request: &request, headers: headers)
        
        URLSession.shared.downloadTask(with: request) { tempURL, response, error in
            DispatchQueue.main.async {
                if let error = error {
                    completion(.failure(error))
                    return
                }
                
                guard let tempURL = tempURL else {
                    completion(.failure(NSError(domain: "No file downloaded", code: -3)))
                    return
                }
                
                do {
                    let directory = cacheURL.deletingLastPathComponent()
                    if !FileManager.default.fileExists(atPath: directory.path) {
                        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
                    }
                    
                    if FileManager.default.fileExists(atPath: cacheURL.path) {
                        try FileManager.default.removeItem(at: cacheURL)
                    }
                    try FileManager.default.moveItem(at: tempURL, to: cacheURL)
                    
                    completion(.success(cacheURL.path))
                } catch {
                    completion(.failure(error))
                }
            }
        }.resume()
    }
    
    private func logRequest(_ request: URLRequest) {
        print("➡️ Request URL: \(request.url?.absoluteString ?? "")")
        print("➡️ Method: \(request.httpMethod ?? "")")
        print("➡️ Headers: \(request.allHTTPHeaderFields ?? [:])")
        if let body = request.httpBody {
            if let bodyString = String(data: body, encoding: .utf8) {
                print("➡️ Body:")
                bodyString.jsonPrint()
            } else {
                let sizeInMB = Double(body.count) / (1024 * 1024)
                print(String(format: "➡️ Body (binary data, size: %.2f MB)", sizeInMB))
            }
        }
    }
    
    private func logResponse(data: Data) {
        if let responseString = String(data: data, encoding: .utf8) {
            print("⬅️ Response:")
            responseString.jsonPrint()
        } else {
            print("⬅️ Response (binary data, length: \(data.count) bytes)")
        }
    }
    
    private func mimeType(for data: Data) -> String {
        var bytes = [UInt8](repeating: 0, count: 1)
        data.copyBytes(to: &bytes, count: 1)
        switch bytes[0] {
        case 0xFF: return "image/jpeg"
        case 0x89: return "image/png"
        case 0x47: return "image/gif"
        case 0x25: return "application/pdf"
        case 0x49, 0x4D: return "image/tiff"
        default: return "application/octet-stream"
        }
    }
}

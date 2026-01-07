/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import Foundation

class WebSocketUtil {
    static let shared = WebSocketUtil()
    private init() {}
    
    private var webSocketTask: URLSessionWebSocketTask?
    
    private var observers = [String: (URLSessionWebSocketTask.Message) -> Void]()
    
    private var reconnectDelay: TimeInterval = 3.0
    private var reconnectingNow: Bool = true
    private var urlString: String = ""
    
    func connect(urlString: String) {
        if let task = webSocketTask {
            switch task.state {
            case .running, .suspended :
                disconnect()
            default:
                break
            }
        }
        
        self.urlString = urlString
        
        print("Start connecting to WebSocket:" + urlString)
        
        guard let url = URL(string: urlString) else {
            print("Invalid URL")
            return
        }
        
        let request = URLRequest(url: url)
        let session = URLSession(configuration: .default)
        
        webSocketTask = session.webSocketTask(with: request)
        
        webSocketTask?.resume()
        
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
            if self.webSocketTask?.state  == .running {
                print("webSocket Connection successful")
                self.reconnectingNow = false
                self.webSocketTask?.receive { [weak self] result in
                    self?.handleReceive(result: result)
                }
            } else {
                print("webSocket Connection failed. Reconnecting is being prepared.")
                self.reconnectingNow = true
                DispatchQueue.main.asyncAfter(deadline: .now() + self.reconnectDelay) {
                    self.connect(urlString: urlString)
                }
            }
        }
    }
    
    private func handleReceive(result: Result<URLSessionWebSocketTask.Message, Error>) {
        switch result {
        case .success(let message):
            let isPing = replyPong(message: message)
            if !isPing {
                self.notifyObservers(message: message)
            }
            self.webSocketTask?.receive { [weak self] next in
                self?.handleReceive(result: next)
            }
            
        case .failure(let error):
            print("❌ Failed to receive the message: \(error.localizedDescription)")
            print("⚠️ WebSocket Connection lost. Attempting to reconnect.…")
            if !self.reconnectingNow && !self.urlString.isEmpty {
                self.connect(urlString: self.urlString)
            }
        }
    }
    
    func replyPong(message: URLSessionWebSocketTask.Message) -> Bool {
        switch message {
        case .data(let data):
            let result = AppState.shared.parseMessage(message: data)
            if let msgType = result.0, let _ = result.1 {
                switch msgType {
                case MsgType.ping:
                    AppState.shared.sendWebSocketMessage(.pong)
                    return true
                default:
                    return false
                }
            }
        case .string(_):
            return false
        @unknown default:
            return false
        }
        return false
    }
    
    
    func send(message: String) {
        let wsMessage = URLSessionWebSocketTask.Message.string(message)
        print("This is the message being sent.: \(message)")
        webSocketTask?.send(wsMessage) { error in
            if let error = error {
                print("❌ Message sending failed: \(error.localizedDescription)")
                if !self.reconnectingNow && !self.urlString.isEmpty {
                    self.connect(urlString: self.urlString)
                }
            }
        }
    }
    
    func send(data: Data) {
        let wsMessage = URLSessionWebSocketTask.Message.data(data)
        webSocketTask?.send(wsMessage) { error in
            if let error = error {
                print("❌ Failed to send binary message: \(error.localizedDescription)")
                if !self.reconnectingNow && !self.urlString.isEmpty {
                    self.connect(urlString: self.urlString)
                }
            }
        }
    }
    
    func disconnect() {
        webSocketTask?.cancel(with: .goingAway, reason: nil)
        self.reconnectingNow = false
        print("webSocket Disconnected")
    }
    
    func addObserver(for key: String, observer: @escaping (URLSessionWebSocketTask.Message) -> Void) {
        observers[key] = observer
    }
    
    func removeObserver(for key: String) {
        observers.removeValue(forKey: key)
    }
    
    func removeAllObservers() {
        observers.removeAll()
    }
    
    func notifyObservers(message: URLSessionWebSocketTask.Message) {
        for observer in observers.values {
            observer(message)
        }
    }
}

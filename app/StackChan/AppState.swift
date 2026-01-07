/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import Combine
import SwiftUI
import CoreBluetooth
import ARKit
import Combine

enum PageType: Hashable {
    case minicryEmotion
    case cameraPage
    case dance
}

class AppState: ObservableObject {
    static let shared = AppState()
    private init() {}
    
    static let deviceId = UIDevice.current.identifierForVendor?.uuidString ?? UUID().uuidString
    
    static var isRelease : Bool = true
    @Published var showAlert: Bool = false
    @Published var alertTitle: String = ""
    var alertAction: (() -> Void)? = nil
    
    func presentAlert(title: String,action: (() -> Void)? = nil) {
        self.alertTitle = title
        self.alertAction = action
        self.showAlert = true
    }
    
    @AppStorage("deviceMac") var deviceMac: String = ""
    
    @Published var stackChanPath: [PageType] = []
    @Published var nearbyPath: [PageType] = []
    @Published var settingsPath: [PageType] = []
    
    @Published var showBindingDevice = false
    @Published var forcedDisplayBindingDevice = true
    
    @Published var showCjamgeNameAlert: Bool = false
    
    @Published var showBindingDeviceAlert: Bool = false
    
    @Published var newName: String = ""
    @Published var deviceInfo: Device = Device()
    
    let detector = DistanceDetector()
    @Published var showSwitchFace: Bool = false
    
    @Published var blufDeviceList: [BlufiDeviceInfo] = []
    
    /// Whether currently pairing a device
    @Published var showDeviceWifiSet = false
    
    // Manual shutdown time, if just manually shut down, temporarily do not configure
    var manualShutdownTime: Date? = nil
    
    @Published var deviceIsOnline: Bool = false
    
    func connectBulDevice(macAddress: String) {
        if BlufiUtil.shared.blueSwitch {
            BlufiUtil.shared.startScan()
        } else {
            BlufiUtil.shared.centralManagerDidUpdateState = { state in
                switch state {
                case .poweredOn:
                    BlufiUtil.shared.startScan()
                default: break
                }
            }
        }
        BlufiUtil.shared.characteristicCallback = { characteristic in
            if characteristic.properties.contains(.write) || characteristic.properties.contains(.writeWithoutResponse) {
                
                if characteristic.uuid.uuidString == "E2E5E5E2-1234-5678-1234-56789ABCDEF0" {
                    BlufiUtil.shared.writeExpressionCharacteristic = characteristic
                    print("✏️ Expression writable characteristic assigned: \(characteristic.uuid)")
                }
                
                if characteristic.uuid.uuidString == "E2E5E5E1-1234-5678-1234-56789ABCDEF0" {
                    BlufiUtil.shared.writeHeadCharacteristic = characteristic
                    print("✏️ Head writable characteristic assigned: \(characteristic.uuid)")
                }
            }
        }
    }
    
    func connectWebSocket() {
        let webSocketUrl = Urls.getWebSocketUrl() + "?mac=" + deviceMac + "&deviceType=App&deviceId=" + AppState.deviceId
        WebSocketUtil.shared.connect(urlString: webSocketUrl)
    }
    
    func sendWebSocketMessage(_ msgType: MsgType,_ data: Data? = nil) {
        var buffer = Data([msgType.rawValue])
        
        let payload = data ?? Data()
        
        // payload length
        let dataLen = UInt32(payload.count)
        buffer.append(UInt8((dataLen >> 24) & 0xFF))
        buffer.append(UInt8((dataLen >> 16) & 0xFF))
        buffer.append(UInt8((dataLen >> 8) & 0xFF))
        buffer.append(UInt8(dataLen & 0xFF))
        
        // data
        buffer.append(payload)
        
        WebSocketUtil.shared.send(data: buffer)
    }
    
    /// Parse message data
    func parseMessage(message: Data) -> (MsgType?,Data?) {
        guard message.count >= 5 else {
            return (nil,nil)
        }
        
        let typeByte = message[0]
        guard let msgType = MsgType(rawValue: typeByte) else {
            return (nil,nil)
        }
        
        let lengthData = message[1...4]
        let dataLength = lengthData.reduce(0) { (result, byte) -> UInt32 in
            return (result << 8) | UInt32(byte)
        }
        
        if message.count < 5 + Int(dataLength) {
            return (nil,nil)
        }
        
        let payload = message[5..<(5 + Int(dataLength))]
        
        return (msgType, Data(payload))
    }
    
    func updateDeviceInfo() {
        let map = [
            ValueConstant.mac: deviceMac,
            ValueConstant.name: deviceInfo.name,
        ]
        Networking.shared.put(pathUrl: Urls.deviceInfo, parameters: map) { result in
            switch result {
            case .success(let success):
                do {
                    let response = try Response<String>.decode(from: success)
                    if response.isSuccess {
                        print("Update successful")
                    }
                } catch {
                    print("Failed to parse data")
                }
            case .failure(let failure):
                print("Request failed:", failure)
            }
        }
    }
    
    /// Distance detection, callback when close
    func startDistanceDetection() {
        detector.startDistanceDetection(
            distanceUpdate: { distance in
                let distanceInCm = distance * 100
                if distanceInCm < 5 {
                    if self.showSwitchFace == false {
                        self.showSwitchFace = true
                    }
                }
            },
            belowThreshold: {
                // Execute your business logic
                // For example: stop machine, send notification, etc.
            }
        )
    }
    
    func stopDistanceDetection() {
        detector.stopDistanceDetection()
    }
    
    // Wrapper for ARSessionDelegate
    class ARSessionDelegateWrapper: NSObject, ARSessionDelegate {
        var onFrameUpdate: (ARFrame) -> Void
        
        init(onFrameUpdate: @escaping (ARFrame) -> Void) {
            self.onFrameUpdate = onFrameUpdate
        }
        
        func session(_ session: ARSession, didUpdate frame: ARFrame) {
            onFrameUpdate(frame)
        }
    }
    
    func getDeviceInfo() {
        let map = [
            ValueConstant.mac: deviceMac
        ]
        Networking.shared.get(pathUrl: Urls.deviceInfo,parameters: map) { result in
            switch result {
            case .success(let success):
                do {
                    let response = try Response<Device>.decode(from: success)
                    if response.isSuccess, let deviceInfo = response.data {
                        withAnimation {
                            self.deviceInfo = deviceInfo
                            self.newName = self.deviceInfo.name ?? ""
                        }
                        if deviceInfo.name == "" {
                            self.showCjamgeNameAlert = true
                        }
                    }
                } catch {
                    print("Failed to parse data")
                }
            case .failure(let failure):
                print("Request failed:", failure)
            }
        }
    }
    
    /// Enable Bluetooth functionality
    func openBlufi() {
        BlufiUtil.shared.blufDevicesMonitoring = { discovereDevices in
            self.blufDeviceList = discovereDevices
            
            // Check manualShutdownTime, if exists and not exceeding 5 seconds, temporarily do not show popup
            if let shutdownTime = self.manualShutdownTime {
                let timeInterval = Date().timeIntervalSince(shutdownTime)
                if timeInterval < 5 {
                    return
                }
            }
            if !self.showDeviceWifiSet {
                if !self.blufDeviceList.isEmpty {
                    self.showDeviceWifiSet = true
                }
            }
        }
    }
    
    // webSocket Message Monitoring
    func webSocketMessageMonitoring() {
        WebSocketUtil.shared.addObserver(for: "App") { (message: URLSessionWebSocketTask.Message) in
            switch message {
            case .data(let data):
                let result = self.parseMessage(message: data)
                if let msgType = result.0 {
                    switch msgType {
                    case MsgType.deviceOnline:
                        self.deviceIsOnline = false
                    case MsgType.deviceOffline:
                        self.deviceIsOnline = true
                    default:
                        break
                    }
                }
            case .string(let text):
                print("Received a regular message: \(text)")
            @unknown default:
                break
            }
        }
    }
}

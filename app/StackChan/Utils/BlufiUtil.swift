/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import ObjectiveC
import CoreBluetooth

class BlufiUtil: NSObject,CBCentralManagerDelegate,CBPeripheralDelegate {
    
    static let shared = BlufiUtil()
    
    private var centralManager: CBCentralManager!
    var discovereDevices: [BlufiDeviceInfo] = []
    
    var blufDevicesMonitoring: (([BlufiDeviceInfo]) -> Void)?
    var centralManagerDidUpdateState: ((CBManagerState) -> Void)?
    var characteristicCallback: ((CBCharacteristic) -> Void)?
    
    var connectionStateChanged: ((CBPeripheral, Bool) -> Void)?
    
    var blueSwitch: Bool = false
    private let autoReconnect = true
    var currentPeripheral: CBPeripheral? = nil
    var writeCharacteristic: CBCharacteristic? = nil
    var automaticScanning: Bool = true
    
    var writeExpressionCharacteristic: CBCharacteristic? = nil
    var writeHeadCharacteristic: CBCharacteristic? = nil
    var writeWifiSetCharacteristic: CBCharacteristic? = nil
    
    var wifiSetCharacteristicCall: ((Data) -> Void)? = nil
    
    /// Service UUIDs. The firmware advertises two distinct 128-bit UUIDs:
    /// - Base  (e2e5e5e0-...) exposed by startBleServer() — runtime control
    /// - Alt   (e2e5e5ff-...) exposed by startAppConfigServer() — pairing/Wi-Fi setup
    /// Scanning for both lets the app find the device in both modes.
    private let targetServiceUUIDs: [CBUUID] = [
        CBUUID(string: "e2e5e5e0-1234-5678-1234-56789abcdef0"),
        CBUUID(string: "e2e5e5ff-1234-5678-1234-56789abcdef0"),
    ]
    
    private let scanOptions: [String: Any] = [
        CBCentralManagerScanOptionAllowDuplicatesKey: true
    ]
    /// Timer to clean up devices that are not discovered
    private var cleanupTimer: Timer?
    private let deviceTimeout: TimeInterval = 3
    
    private override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: .main)
    }
    
    // Print logs according to Bluetooth state
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        centralManagerDidUpdateState?(central.state)
        switch central.state {
        case .unknown:
            print("Bluetooth state unknown")
        case .resetting:
            print("Bluetooth is resetting")
        case .unsupported:
            print("This device does not support Bluetooth")
        case .unauthorized:
            print("No permission to use Bluetooth, please check settings")
        case .poweredOff:
            print("Bluetooth is off, please turn on Bluetooth")
            blueSwitch = false
        case .poweredOn:
            print("Bluetooth is on, ready to start scanning devices")
            blueSwitch = true
            if automaticScanning {
                startScan()
            }
            if autoReconnect {
                reconnect()
            }
        @unknown default:
            print("Encountered unknown Bluetooth state")
        }
    }
    
    // Start scanning BLE devices
    func startScan() {
        guard centralManager.state == .poweredOn else {
            print("Bluetooth is not ready when scanning")
            return
        }
        discovereDevices.removeAll()
        print("Started scanning nearby BLE devices")
        centralManager.scanForPeripherals(withServices: targetServiceUUIDs, options: scanOptions)
        startCleanupTimer()
    }
    
    // Periodically clean up non-existing devices
    private func startCleanupTimer() {
        cleanupTimer?.invalidate()
        cleanupTimer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true, block: { timer in
            let now = Date()
            let originalCount = self.discovereDevices.count
            
            self.discovereDevices.removeAll {
                now.timeIntervalSince($0.lastSeen) > self.deviceTimeout
            }
            
            // If indeed some equipment has been removed, then notify the external party.
            if self.discovereDevices.count != originalCount {
                self.blufDevicesMonitoring?(self.discovereDevices)
            }
        })
    }
    
    func stopScan() {
        print("Stopped scanning BLE devices")
        centralManager.stopScan()
    }
    
    func connect(peripheral: CBPeripheral) {
        print("Started connecting to the specified BLE device")
        centralManager.connect(peripheral)
    }
    
    /// Actively disconnect the current peripheral
    func disconnectCurrentPeripheral() {
        guard let peripheral = currentPeripheral else {
            print("⚠️ No connected peripheral to disconnect")
            return
        }
        centralManager.cancelPeripheralConnection(peripheral)
        print("⬅️ Disconnecting peripheral: \(peripheral.name ?? "Unknown device")")
        writeWifiSetCharacteristic = nil
        writeHeadCharacteristic = nil
        writeExpressionCharacteristic = nil
    }
    
    
    /// Send head data
    func sendHeadData(_ data: String) {
        guard let currentPeripheral = currentPeripheral else {
            print("⚠️ No connected peripheral")
            return
        }
        
        guard let writeHeadCharacteristic = self.writeHeadCharacteristic else {
            print("⚠️ No writable characteristic on current peripheral")
            return
        }
        
        guard let dataToSend = data.data(using: .utf8) else {
            print("⚠️ Failed to convert string to Data")
            return
        }
        
        currentPeripheral.writeValue(dataToSend, for: writeHeadCharacteristic, type: .withResponse)
        print("➡️ Head data sent: \(data)")
    }
    
    /// Send Wi-Fi configuration data
    func sendWifiSetData(_ data: String) {
        guard let currentPeripheral = currentPeripheral else {
            print("⚠️ No connected peripheral")
            return
        }
        guard let writeWifiSetCharacteristic = self.writeWifiSetCharacteristic else {
            print("⚠️ No writable characteristic on current peripheral")
            return
        }
        guard let dataToSend = data.data(using: .utf8) else {
            print("⚠️ Failed to convert string to Data")
            return
        }
        currentPeripheral.writeValue(dataToSend, for: writeWifiSetCharacteristic, type: .withResponse)
        print("➡️ Head data sent: \(data)")
    }
    
    /// Send expression data
    func sendExpressionData(_ data: String) {
        guard let currentPeripheral = currentPeripheral else {
            print("⚠️ No connected peripheral")
            return
        }
        
        guard let writeExpressionCharacteristic = self.writeExpressionCharacteristic else {
            print("⚠️ No writable characteristic on current peripheral")
            return
        }
        
        guard let dataToSend = data.data(using: .utf8) else {
            print("⚠️ Failed to convert string to Data")
            return
        }
        
        currentPeripheral.writeValue(dataToSend, for: writeExpressionCharacteristic, type: .withResponse)
        print("➡️ Expression data sent: \(data)")
    }
    
    func sendData(_ data: String) {
        guard let currentPeripheral = currentPeripheral else {
            print("⚠️ No connected peripheral")
            return
        }
        
        guard let writeCharacteristic = self.writeCharacteristic else {
            print("⚠️ No writable characteristic on current peripheral")
            return
        }
        
        guard let dataToSend = data.data(using: .utf8) else {
            print("⚠️ Failed to convert string to Data")
            return
        }
        
        currentPeripheral.writeValue(dataToSend, for: writeCharacteristic, type: .withResponse)
        print("➡️ Data sent: \(data)")
    }
    
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        // Called when successfully connected to a peripheral
        print("✅ Successfully connected to device: \(peripheral.name ?? "Unknown device")")
        self.currentPeripheral = peripheral
        peripheral.delegate = self
        peripheral.discoverServices(nil)
        connectionStateChanged?(peripheral,true)
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: (any Error)?) {
        // Called when connecting to a peripheral fails
        if let error = error {
            print("❌ Failed to connect to device: \(peripheral.name ?? "Unknown device"), error: \(error.localizedDescription)")
        } else {
            print("❌ Failed to connect to device: \(peripheral.name ?? "Unknown device"), unknown error")
        }
        
        connectionStateChanged?(peripheral,false)
    }
    
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: (any Error)?) {
        // Called when a peripheral disconnects
        if let error = error {
            print("⚠️ Device disconnected: \(peripheral.name ?? "Unknown device"), error: \(error.localizedDescription)")
        } else {
            print("⚠️ Device disconnected: \(peripheral.name ?? "Unknown device"), no error")
        }
        currentPeripheral = nil
        
        connectionStateChanged?(peripheral,false)
        
        if autoReconnect {
            reconnect()
        }
    }
    
    func centralManager(_ central: CBCentralManager, didUpdateANCSAuthorizationFor peripheral: CBPeripheral) {
        // Called when ANCS (Apple Notification Center Service) authorization status changes
        print("ℹ️ ANCS authorization status updated, device: \(peripheral.name ?? "Unknown device")")
    }
    
    func centralManager(_ central: CBCentralManager, connectionEventDidOccur event: CBConnectionEvent, for peripheral: CBPeripheral) {
        // Called when a connection event occurs (e.g., peripheral connected or disconnected)
        print("🔔 Connection event occurred: \(event.rawValue), device: \(peripheral.name ?? "Unknown device")")
    }
    
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String : Any], rssi RSSI: NSNumber) {
        // New device added to the list
        let deviceInfo = BlufiDeviceInfo(peripheral: peripheral, advertisementData: advertisementData, rssi: RSSI, lastSeen: Date())
        
        if !discovereDevices.contains(where: { $0.peripheral.identifier.uuidString == peripheral.identifier.uuidString }) {
            discovereDevices.append(deviceInfo)
        } else {
            if let index = discovereDevices.firstIndex(where: { $0.peripheral.identifier.uuidString == peripheral.identifier.uuidString }) {
                discovereDevices[index] = deviceInfo
            }
        }
        blufDevicesMonitoring?(discovereDevices)
    }
    
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, timestamp: CFAbsoluteTime, isReconnecting: Bool, error: (any Error)?) {
        if let error = error {
            print("⚠️ Device disconnected: \(peripheral.name ?? "Unknown device"), timestamp: \(timestamp), reconnecting: \(isReconnecting), error: \(error.localizedDescription)")
        } else {
            print("⚠️ Device disconnected: \(peripheral.name ?? "Unknown device"), timestamp: \(timestamp), reconnecting: \(isReconnecting), no error")
        }
    }
    
    func peripheralDidUpdateName(_ peripheral: CBPeripheral) {
        // Called when peripheral name updates
        print("ℹ️ Peripheral name updated: \(peripheral.name ?? "Unknown device")")
    }
    
    func peripheralDidUpdateRSSI(_ peripheral: CBPeripheral, error: (any Error)?) {
        // Called when peripheral RSSI (signal strength) updates
        if let error = error {
            print("❌ Failed to update peripheral RSSI: \(error.localizedDescription)")
        } else {
            print("ℹ️ Peripheral RSSI updated")
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didOpen channel: CBL2CAPChannel?, error: (any Error)?) {
        // Called when L2CAP channel opens
        if let error = error {
            print("❌ Failed to open L2CAP channel: \(error.localizedDescription)")
        } else {
            print("✅ L2CAP channel opened: \(channel?.psm ?? 0)")
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didModifyServices invalidatedServices: [CBService]) {
        // Called when peripheral services are modified
        print("⚠️ Peripheral services modified, number of invalidated services: \(invalidatedServices.count)")
    }
    
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: (any Error)?) {
        // Called after discovering services
        if let error = error {
            print("❌ Failed to discover services: \(error.localizedDescription)")
            return
        }
        print("✅ Discovered peripheral services, service count: \(peripheral.services?.count ?? 0)")
        peripheral.services?.forEach { service in
            print("📦 Service UUID: \(service.uuid)")
            peripheral.discoverCharacteristics(nil, for: service)
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didReadRSSI RSSI: NSNumber, error: (any Error)?) {
        // Called when RSSI value is read
        if let error = error {
            print("❌ Failed to read RSSI: \(error.localizedDescription)")
        } else {
            print("ℹ️ Read RSSI: \(RSSI)")
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: (any Error)?) {
    }
    
    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor descriptor: CBDescriptor, error: (any Error)?) {
        // Called after writing a descriptor value
        if let error = error {
            print("❌ Failed to write descriptor value: \(descriptor.uuid), error: \(error.localizedDescription)")
        } else {
            print("✅ Successfully wrote descriptor value: \(descriptor.uuid)")
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: (any Error)?) {
        // Called when characteristic value updates (message received)
        if let error = error {
            print("❌ Failed to update characteristic value: \(characteristic.uuid), error: \(error.localizedDescription)")
        } else {
            print("Bluetooth message received")
            if characteristic.uuid.uuidString == writeWifiSetCharacteristic?.uuid.uuidString, let data = characteristic.value {
                /// Callback for Wi-Fi configuration message
                self.wifiSetCharacteristicCall?(data)
            }
            //            print("ℹ️ Characteristic value updated: \(characteristic.uuid), value: \(characteristic.value?.hexEncodedString() ?? "nil")")
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor descriptor: CBDescriptor, error: (any Error)?) {
        // Called when descriptor value updates
        if let error = error {
            print("❌ Failed to update descriptor value: \(descriptor.uuid), error: \(error.localizedDescription)")
        } else {
            print("ℹ️ Descriptor value updated: \(descriptor.uuid), value: \(descriptor.value ?? "nil")")
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didDiscoverDescriptorsFor characteristic: CBCharacteristic, error: (any Error)?) {
        // Called when descriptors of a characteristic are discovered
        if let error = error {
            print("❌ Failed to discover descriptors: \(characteristic.uuid), error: \(error.localizedDescription)")
        } else {
            print("✅ Discovered characteristic descriptors: \(characteristic.uuid), descriptor count: \(characteristic.descriptors?.count ?? 0)")
        }
    }
    
    func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) {
        // Called when peripheral is ready to send writes without response
        print("ℹ️ Peripheral is ready to send write without response: \(peripheral.name ?? "Unknown device")")
    }
    
    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: (any Error)?) {
        // Called when characteristics of a service are discovered
        if let error = error {
            print("❌ Failed to discover characteristics: \(service.uuid), error: \(error.localizedDescription)")
            return
        }
        print("✅ Discovered characteristics count: \(service.characteristics?.count ?? 0) for service: \(service.uuid)")
        service.characteristics?.forEach { characteristic in
            print("🔹 Characteristic UUID: \(characteristic.uuid), properties: \(characteristic.properties)")
            characteristicCallback?(characteristic)
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: (any Error)?) {
        // Called when characteristic notification state updates
        if let error = error {
            print("❌ Failed to update characteristic notification state: \(characteristic.uuid), error: \(error.localizedDescription)")
        } else {
            print("ℹ️ Characteristic notification state updated: \(characteristic.uuid), isNotifying: \(characteristic.isNotifying)")
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didDiscoverIncludedServicesFor service: CBService, error: (any Error)?) {
        // Called when included services are discovered
        if let error = error {
            print("❌ Failed to discover included services: \(service.uuid), error: \(error.localizedDescription)")
        } else {
            print("✅ Discovered included services count: \(service.includedServices?.count ?? 0) for service: \(service.uuid)")
        }
    }
    
    // Method to reconnect
    func reconnect() {
        
    }
    
}


struct BlufiDeviceInfo {
    let peripheral: CBPeripheral
    let advertisementData: [String : Any]
    let rssi: NSNumber
    var lastSeen: Date
}


extension Data {
    func hexEncodedString() -> String {
        return map { String(format: "%02hhx", $0) }.joined()
    }
}

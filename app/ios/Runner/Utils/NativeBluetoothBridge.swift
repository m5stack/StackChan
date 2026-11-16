/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import CoreBluetooth
import Flutter
import Foundation

final class NativeBluetoothBridge: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate, FlutterStreamHandler {
    static let shared = NativeBluetoothBridge()

    private let methodChannelName = "com.m5stack.stackchan/bluetooth"
    private let eventChannelName = "com.m5stack.stackchan/bluetooth_events"

    private var methodChannel: FlutterMethodChannel?
    private var eventChannel: FlutterEventChannel?
    private var eventSink: FlutterEventSink?
    private var centralManager: CBCentralManager?

    private var peripherals: [String: CBPeripheral] = [:]
    private var pendingConnect: [String: PendingResult] = [:]
    private var pendingDiscover: [String: PendingDiscover] = [:]
    private var pendingNotify: [String: PendingResult] = [:]
    private var pendingWrite: [String: PendingResult] = [:]
    private var pendingRssi: [String: PendingResult] = [:]

    private override init() {
        super.init()
    }

    func setup(with messenger: FlutterBinaryMessenger) {
        methodChannel = FlutterMethodChannel(name: methodChannelName, binaryMessenger: messenger)
        eventChannel = FlutterEventChannel(name: eventChannelName, binaryMessenger: messenger)
        methodChannel?.setMethodCallHandler { [weak self] call, result in
            self?.handle(call: call, result: result)
        }
        eventChannel?.setStreamHandler(self)
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }

    func onListen(withArguments arguments: Any?, eventSink events: @escaping FlutterEventSink) -> FlutterError? {
        eventSink = events
        sendEvent(["type": "adapterStateChanged", "state": adapterStateString()])
        return nil
    }

    func onCancel(withArguments arguments: Any?) -> FlutterError? {
        eventSink = nil
        return nil
    }

    private func handle(call: FlutterMethodCall, result: @escaping FlutterResult) {
        switch call.method {
        case "getAdapterState":
            result(adapterStateString())
        case "turnOn":
            result(false)
        case "startScan":
            startScan(call: call, result: result)
        case "stopScan":
            centralManager?.stopScan()
            result(nil)
        case "connect":
            connect(call: call, result: result)
        case "disconnect":
            disconnect(call: call, result: result)
        case "discoverServices":
            discoverServices(call: call, result: result)
        case "setNotifyValue":
            setNotifyValue(call: call, result: result)
        case "writeCharacteristic":
            writeCharacteristic(call: call, result: result)
        case "requestMtu":
            requestMtu(call: call, result: result)
        case "readRssi":
            readRssi(call: call, result: result)
        default:
            result(FlutterMethodNotImplemented)
        }
    }

    private func startScan(call: FlutterMethodCall, result: @escaping FlutterResult) {
        guard let centralManager = centralManager else {
            result(flutterError("unavailable", "Bluetooth is unavailable"))
            return
        }
        guard centralManager.state == .poweredOn else {
            result(flutterError("adapter_off", "Bluetooth is off"))
            return
        }
        let arguments = call.arguments as? [String: Any]
        let serviceUuids = (arguments?["serviceUuids"] as? [String] ?? []).map { CBUUID(string: $0) }
        centralManager.stopScan()
        centralManager.scanForPeripherals(
            withServices: serviceUuids.isEmpty ? nil : serviceUuids,
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: true]
        )
        result(nil)
    }

    private func connect(call: FlutterMethodCall, result: @escaping FlutterResult) {
        guard let centralManager = centralManager else {
            result(flutterError("unavailable", "Bluetooth is unavailable"))
            return
        }
        do {
            let deviceId = try requiredString(call: call, key: "deviceId")
            let timeoutMillis = argumentNumber(call: call, key: "timeoutMillis")?.doubleValue ?? 15_000
            guard let peripheral = peripheral(for: deviceId) else {
                result(flutterError("not_found", "Bluetooth device was not found"))
                return
            }
            peripheral.delegate = self
            if peripheral.state == .connected {
                sendConnectionState(deviceId: deviceId, state: "connected", reason: nil)
                result(nil)
                return
            }
            let timeout = DispatchWorkItem { [weak self] in
                guard let self = self else { return }
                if let pending = self.pendingConnect.removeValue(forKey: deviceId) {
                    pending.fail(code: "timeout", message: "Bluetooth connection timed out")
                    centralManager.cancelPeripheralConnection(peripheral)
                }
            }
            pendingConnect[deviceId] = PendingResult(result: result, timeout: timeout)
            DispatchQueue.main.asyncAfter(deadline: .now() + timeoutMillis / 1000.0, execute: timeout)
            centralManager.connect(peripheral, options: nil)
        } catch {
            result(flutterError("invalid_argument", error.localizedDescription))
        }
    }

    private func disconnect(call: FlutterMethodCall, result: @escaping FlutterResult) {
        do {
            let deviceId = try requiredString(call: call, key: "deviceId")
            if let peripheral = peripherals[deviceId] {
                centralManager?.cancelPeripheralConnection(peripheral)
            }
            result(nil)
        } catch {
            result(flutterError("invalid_argument", error.localizedDescription))
        }
    }

    private func discoverServices(call: FlutterMethodCall, result: @escaping FlutterResult) {
        do {
            let deviceId = try requiredString(call: call, key: "deviceId")
            let timeoutMillis = argumentNumber(call: call, key: "timeoutMillis")?.doubleValue ?? 35_000
            guard let peripheral = connectedPeripheral(deviceId: deviceId, result: result) else { return }
            let timeout = DispatchWorkItem { [weak self] in
                if let pending = self?.pendingDiscover.removeValue(forKey: deviceId) {
                    pending.fail(code: "timeout", message: "Service discovery timed out")
                }
            }
            pendingDiscover[deviceId] = PendingDiscover(result: result, timeout: timeout)
            DispatchQueue.main.asyncAfter(deadline: .now() + timeoutMillis / 1000.0, execute: timeout)
            peripheral.discoverServices(nil)
        } catch {
            result(flutterError("invalid_argument", error.localizedDescription))
        }
    }

    private func setNotifyValue(call: FlutterMethodCall, result: @escaping FlutterResult) {
        do {
            let deviceId = try requiredString(call: call, key: "deviceId")
            let serviceUuid = try requiredString(call: call, key: "serviceUuid")
            let characteristicUuid = try requiredString(call: call, key: "characteristicUuid")
            let enabled = (call.arguments as? [String: Any])?["enabled"] as? Bool ?? false
            guard let peripheral = connectedPeripheral(deviceId: deviceId, result: result) else { return }
            guard let characteristic = findCharacteristic(
                peripheral: peripheral,
                serviceUuid: serviceUuid,
                characteristicUuid: characteristicUuid
            ) else {
                result(flutterError("not_found", "Characteristic was not found"))
                return
            }
            let key = operationKey(deviceId: deviceId, serviceUuid: serviceUuid, characteristicUuid: characteristicUuid)
            let timeout = DispatchWorkItem { [weak self] in
                if let pending = self?.pendingNotify.removeValue(forKey: key) {
                    pending.fail(code: "timeout", message: "Notification request timed out")
                }
            }
            pendingNotify[key] = PendingResult(result: result, timeout: timeout)
            DispatchQueue.main.asyncAfter(deadline: .now() + operationTimeoutSeconds, execute: timeout)
            peripheral.setNotifyValue(enabled, for: characteristic)
        } catch {
            result(flutterError("invalid_argument", error.localizedDescription))
        }
    }

    private func writeCharacteristic(call: FlutterMethodCall, result: @escaping FlutterResult) {
        do {
            let deviceId = try requiredString(call: call, key: "deviceId")
            let serviceUuid = try requiredString(call: call, key: "serviceUuid")
            let characteristicUuid = try requiredString(call: call, key: "characteristicUuid")
            let arguments = call.arguments as? [String: Any]
            let value = typedData(arguments?["value"])
            let withoutResponse = arguments?["withoutResponse"] as? Bool ?? false
            guard let peripheral = connectedPeripheral(deviceId: deviceId, result: result) else { return }
            guard let characteristic = findCharacteristic(
                peripheral: peripheral,
                serviceUuid: serviceUuid,
                characteristicUuid: characteristicUuid
            ) else {
                result(flutterError("not_found", "Characteristic was not found"))
                return
            }
            let useWithoutResponse = withoutResponse ||
                (!characteristic.properties.contains(.write) && characteristic.properties.contains(.writeWithoutResponse))
            let writeType: CBCharacteristicWriteType = useWithoutResponse ? .withoutResponse : .withResponse
            if writeType == .withoutResponse {
                peripheral.writeValue(value, for: characteristic, type: writeType)
                result(nil)
                return
            }
            let key = operationKey(deviceId: deviceId, serviceUuid: serviceUuid, characteristicUuid: characteristicUuid)
            let timeout = DispatchWorkItem { [weak self] in
                if let pending = self?.pendingWrite.removeValue(forKey: key) {
                    pending.fail(code: "timeout", message: "Characteristic write timed out")
                }
            }
            pendingWrite[key] = PendingResult(result: result, timeout: timeout)
            DispatchQueue.main.asyncAfter(deadline: .now() + operationTimeoutSeconds, execute: timeout)
            peripheral.writeValue(value, for: characteristic, type: writeType)
        } catch {
            result(flutterError("invalid_argument", error.localizedDescription))
        }
    }

    private func requestMtu(call: FlutterMethodCall, result: @escaping FlutterResult) {
        do {
            let deviceId = try requiredString(call: call, key: "deviceId")
            guard let peripheral = connectedPeripheral(deviceId: deviceId, result: result) else { return }
            result(peripheral.maximumWriteValueLength(for: .withResponse) + 3)
        } catch {
            result(flutterError("invalid_argument", error.localizedDescription))
        }
    }

    private func readRssi(call: FlutterMethodCall, result: @escaping FlutterResult) {
        do {
            let deviceId = try requiredString(call: call, key: "deviceId")
            let timeoutMillis = argumentNumber(call: call, key: "timeoutMillis")?.doubleValue ?? 15_000
            guard let peripheral = connectedPeripheral(deviceId: deviceId, result: result) else { return }
            let timeout = DispatchWorkItem { [weak self] in
                if let pending = self?.pendingRssi.removeValue(forKey: deviceId) {
                    pending.fail(code: "timeout", message: "RSSI read timed out")
                }
            }
            pendingRssi[deviceId] = PendingResult(result: result, timeout: timeout)
            DispatchQueue.main.asyncAfter(deadline: .now() + timeoutMillis / 1000.0, execute: timeout)
            peripheral.readRSSI()
        } catch {
            result(flutterError("invalid_argument", error.localizedDescription))
        }
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        sendEvent(["type": "adapterStateChanged", "state": adapterStateString()])
    }

    func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        let deviceId = peripheral.identifier.uuidString
        peripherals[deviceId] = peripheral
        sendEvent([
            "type": "scanResult",
            "result": scanResultMap(peripheral: peripheral, advertisementData: advertisementData, rssi: RSSI),
        ])
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        let deviceId = peripheral.identifier.uuidString
        peripheral.delegate = self
        sendConnectionState(deviceId: deviceId, state: "connected", reason: nil)
        complete(&pendingConnect, key: deviceId, value: nil)
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        let deviceId = peripheral.identifier.uuidString
        let message = error?.localizedDescription ?? "Bluetooth connection failed"
        sendConnectionState(deviceId: deviceId, state: "disconnected", reason: message)
        fail(&pendingConnect, key: deviceId, code: "connect_failed", message: message)
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        let deviceId = peripheral.identifier.uuidString
        sendConnectionState(deviceId: deviceId, state: "disconnected", reason: error?.localizedDescription)
        fail(&pendingConnect, key: deviceId, code: "disconnected", message: error?.localizedDescription ?? "Bluetooth disconnected")
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        let deviceId = peripheral.identifier.uuidString
        if let error = error {
            fail(&pendingDiscover, key: deviceId, code: "discover_failed", message: error.localizedDescription)
            return
        }
        guard let services = peripheral.services, !services.isEmpty else {
            completeDiscover(deviceId: deviceId, peripheral: peripheral)
            return
        }
        guard let pending = pendingDiscover[deviceId] else { return }
        pending.pendingServiceUuids = Set(services.map { uuidString($0.uuid) })
        for service in services {
            peripheral.discoverCharacteristics(nil, for: service)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        let deviceId = peripheral.identifier.uuidString
        if let error = error {
            fail(&pendingDiscover, key: deviceId, code: "discover_failed", message: error.localizedDescription)
            return
        }
        guard let pending = pendingDiscover[deviceId] else { return }
        pending.pendingServiceUuids.remove(uuidString(service.uuid))
        if pending.pendingServiceUuids.isEmpty {
            completeDiscover(deviceId: deviceId, peripheral: peripheral)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        guard let service = characteristic.service else { return }
        let key = operationKey(
            deviceId: peripheral.identifier.uuidString,
            serviceUuid: uuidString(service.uuid),
            characteristicUuid: uuidString(characteristic.uuid)
        )
        if let error = error {
            fail(&pendingNotify, key: key, code: "notify_failed", message: error.localizedDescription)
        } else {
            complete(&pendingNotify, key: key, value: true)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        guard let service = characteristic.service else { return }
        let key = operationKey(
            deviceId: peripheral.identifier.uuidString,
            serviceUuid: uuidString(service.uuid),
            characteristicUuid: uuidString(characteristic.uuid)
        )
        if let error = error {
            fail(&pendingWrite, key: key, code: "write_failed", message: error.localizedDescription)
        } else {
            complete(&pendingWrite, key: key, value: nil)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard error == nil, let value = characteristic.value, let service = characteristic.service else { return }
        sendEvent([
            "type": "characteristicChanged",
            "deviceId": peripheral.identifier.uuidString,
            "serviceUuid": uuidString(service.uuid),
            "characteristicUuid": uuidString(characteristic.uuid),
            "value": FlutterStandardTypedData(bytes: value),
        ])
    }

    func peripheral(_ peripheral: CBPeripheral, didReadRSSI RSSI: NSNumber, error: Error?) {
        let deviceId = peripheral.identifier.uuidString
        if let error = error {
            fail(&pendingRssi, key: deviceId, code: "rssi_failed", message: error.localizedDescription)
        } else {
            complete(&pendingRssi, key: deviceId, value: RSSI.intValue)
        }
    }

    private func completeDiscover(deviceId: String, peripheral: CBPeripheral) {
        complete(&pendingDiscover, key: deviceId, value: serviceMaps(peripheral: peripheral))
    }

    private func scanResultMap(peripheral: CBPeripheral, advertisementData: [String: Any], rssi: NSNumber) -> [String: Any] {
        let advName = advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? peripheral.name ?? ""
        let serviceUuids = (advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []).map { uuidString($0) }
        var serviceData: [String: FlutterStandardTypedData] = [:]
        if let rawServiceData = advertisementData[CBAdvertisementDataServiceDataKey] as? [CBUUID: Data] {
            for (uuid, data) in rawServiceData {
                serviceData[uuidString(uuid)] = FlutterStandardTypedData(bytes: data)
            }
        }
        var manufacturerData: [Int: FlutterStandardTypedData] = [:]
        if let data = advertisementData[CBAdvertisementDataManufacturerDataKey] as? Data {
            if data.count >= 2 {
                let companyId = Int(data[data.startIndex]) | (Int(data[data.index(after: data.startIndex)]) << 8)
                manufacturerData[companyId] = FlutterStandardTypedData(bytes: Data(data.dropFirst(2)))
            } else {
                manufacturerData[0] = FlutterStandardTypedData(bytes: data)
            }
        }
        var advertisement: [String: Any] = [
            "advName": advName,
            "connectable": advertisementData[CBAdvertisementDataIsConnectable] as? Bool ?? false,
            "serviceUuids": serviceUuids,
            "serviceData": serviceData,
            "manufacturerData": manufacturerData,
        ]
        if let txPower = advertisementData[CBAdvertisementDataTxPowerLevelKey] as? NSNumber {
            advertisement["txPowerLevel"] = txPower.intValue
        }
        return [
            "device": [
                "id": peripheral.identifier.uuidString,
                "name": advName,
                "advName": advName,
            ],
            "advertisementData": advertisement,
            "rssi": rssi.intValue,
        ]
    }

    private func serviceMaps(peripheral: CBPeripheral) -> [[String: Any]] {
        return (peripheral.services ?? []).map { service in
            [
                "uuid": uuidString(service.uuid),
                "characteristics": (service.characteristics ?? []).map { characteristicMap($0) },
            ]
        }
    }

    private func characteristicMap(_ characteristic: CBCharacteristic) -> [String: Any] {
        return [
            "uuid": uuidString(characteristic.uuid),
            "properties": [
                "read": characteristic.properties.contains(.read),
                "write": characteristic.properties.contains(.write),
                "writeWithoutResponse": characteristic.properties.contains(.writeWithoutResponse),
                "notify": characteristic.properties.contains(.notify),
                "indicate": characteristic.properties.contains(.indicate),
            ],
        ]
    }

    private func connectedPeripheral(deviceId: String, result: @escaping FlutterResult) -> CBPeripheral? {
        guard let peripheral = peripherals[deviceId] else {
            result(flutterError("not_connected", "Bluetooth device is not connected"))
            return nil
        }
        guard peripheral.state == .connected else {
            result(flutterError("not_connected", "Bluetooth device is not connected"))
            return nil
        }
        return peripheral
    }

    private func peripheral(for deviceId: String) -> CBPeripheral? {
        if let peripheral = peripherals[deviceId] {
            return peripheral
        }
        guard let uuid = UUID(uuidString: deviceId) else { return nil }
        let retrieved = centralManager?.retrievePeripherals(withIdentifiers: [uuid]).first
        if let retrieved = retrieved {
            peripherals[deviceId] = retrieved
        }
        return retrieved
    }

    private func findCharacteristic(peripheral: CBPeripheral, serviceUuid: String, characteristicUuid: String) -> CBCharacteristic? {
        let normalizedServiceUuid = normalizeUuid(serviceUuid)
        let normalizedCharacteristicUuid = normalizeUuid(characteristicUuid)
        let service = peripheral.services?.first { uuidString($0.uuid) == normalizedServiceUuid }
        return service?.characteristics?.first { uuidString($0.uuid) == normalizedCharacteristicUuid }
    }

    private func sendConnectionState(deviceId: String, state: String, reason: String?) {
        var event: [String: Any] = [
            "type": "connectionStateChanged",
            "deviceId": deviceId,
            "state": state,
        ]
        if let reason = reason {
            event["reason"] = reason
        }
        sendEvent(event)
    }

    private func sendEvent(_ event: [String: Any]) {
        eventSink?(event)
    }

    private func adapterStateString() -> String {
        guard let state = centralManager?.state else { return "unknown" }
        switch state {
        case .poweredOn:
            return "on"
        case .poweredOff:
            return "off"
        case .unsupported:
            return "unavailable"
        case .unauthorized:
            return "unauthorized"
        case .resetting:
            return "turningOn"
        case .unknown:
            return "unknown"
        @unknown default:
            return "unknown"
        }
    }

    private func uuidString(_ uuid: CBUUID) -> String {
        return normalizeUuid(uuid.uuidString)
    }

    private func normalizeUuid(_ raw: String) -> String {
        let value = raw.lowercased()
        if value.count == 4 {
            return "0000\(value)-0000-1000-8000-00805f9b34fb"
        }
        if value.count == 8 {
            return "\(value)-0000-1000-8000-00805f9b34fb"
        }
        return value
    }

    private func operationKey(deviceId: String, serviceUuid: String, characteristicUuid: String) -> String {
        return "\(deviceId)|\(normalizeUuid(serviceUuid))|\(normalizeUuid(characteristicUuid))"
    }

    private func complete(_ dictionary: inout [String: PendingResult], key: String, value: Any?) {
        guard let pending = dictionary.removeValue(forKey: key) else { return }
        pending.succeed(value)
    }

    private func complete(_ dictionary: inout [String: PendingDiscover], key: String, value: Any?) {
        guard let pending = dictionary.removeValue(forKey: key) else { return }
        pending.succeed(value)
    }

    private func fail(_ dictionary: inout [String: PendingResult], key: String, code: String, message: String) {
        guard let pending = dictionary.removeValue(forKey: key) else { return }
        pending.fail(code: code, message: message)
    }

    private func fail(_ dictionary: inout [String: PendingDiscover], key: String, code: String, message: String) {
        guard let pending = dictionary.removeValue(forKey: key) else { return }
        pending.fail(code: code, message: message)
    }

    private func requiredString(call: FlutterMethodCall, key: String) throws -> String {
        let arguments = call.arguments as? [String: Any]
        guard let value = arguments?[key] as? String, !value.isEmpty else {
            throw NSError(domain: "NativeBluetoothBridge", code: 1, userInfo: [NSLocalizedDescriptionKey: "Missing required argument: \(key)"])
        }
        return value
    }

    private func argumentNumber(call: FlutterMethodCall, key: String) -> NSNumber? {
        return (call.arguments as? [String: Any])?[key] as? NSNumber
    }

    private func typedData(_ value: Any?) -> Data {
        if let typedData = value as? FlutterStandardTypedData {
            return typedData.data
        }
        if let data = value as? Data {
            return data
        }
        return Data()
    }

    private func flutterError(_ code: String, _ message: String) -> FlutterError {
        return FlutterError(code: code, message: message, details: nil)
    }

    private let operationTimeoutSeconds = 15.0
}

private final class PendingResult {
    let result: FlutterResult
    let timeout: DispatchWorkItem

    init(result: @escaping FlutterResult, timeout: DispatchWorkItem) {
        self.result = result
        self.timeout = timeout
    }

    func succeed(_ value: Any?) {
        timeout.cancel()
        result(value)
    }

    func fail(code: String, message: String) {
        timeout.cancel()
        result(FlutterError(code: code, message: message, details: nil))
    }
}

private final class PendingDiscover {
    let result: FlutterResult
    let timeout: DispatchWorkItem
    var pendingServiceUuids: Set<String> = []

    init(result: @escaping FlutterResult, timeout: DispatchWorkItem) {
        self.result = result
        self.timeout = timeout
    }

    func succeed(_ value: Any?) {
        timeout.cancel()
        result(value)
    }

    func fail(code: String, message: String) {
        timeout.cancel()
        result(FlutterError(code: code, message: message, details: nil))
    }
}

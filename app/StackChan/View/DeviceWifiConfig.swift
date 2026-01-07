/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI
import NetworkExtension
import CoreLocation
import CoreBluetooth

enum BlufDeviceConfigPageType: Hashable {
    case selectDevice
    case wifiConfig
}

struct SelectBlufiDevice : View {
    
    @EnvironmentObject var appState: AppState
    
    @State var path: [BlufDeviceConfigPageType] = []
    
    private func getDeviceId(blufiInfo: BlufiDeviceInfo) -> String? {
        if let manufacturerData = blufiInfo.advertisementData[CBAdvertisementDataManufacturerDataKey] as? Data {
            let companyID = manufacturerData.prefix(2)
            _ = UInt16(littleEndian: companyID.withUnsafeBytes { $0.load(as: UInt16.self) })
            let customData = manufacturerData.suffix(from: 2)
            let address = customData.map { String(format: "%02X", $0) }.joined()
            return address
        }
        return nil
    }
    
    var body: some View {
        NavigationStack(path: $path) {
            List {
                Section(header: Text("StackChan Device List").textCase(nil)) {
                    ForEach(appState.blufDeviceList, id: \.peripheral.identifier.uuidString) { blufiDeviceInfo in
                        Button {
                            if let mac = getDeviceId(blufiInfo: blufiDeviceInfo) {
                                appState.deviceMac = mac
                                appState.connectWebSocket()
                            }
                            print("start connect device")
                            BlufiUtil.shared.connect(peripheral: blufiDeviceInfo.peripheral)
                        } label: {
                            HStack {
                                Image("lateral_image")
                                    .resizable()
                                    .frame(width: 25, height: 25)

                                VStack(alignment: .leading) {
                                    Text("Name: " + (blufiDeviceInfo.peripheral.name ?? "StackChan"))
                                    if let deviceId = getDeviceId(blufiInfo: blufiDeviceInfo) {
                                        Text("Device ID: \(deviceId)")
                                            .font(.caption)
                                            .foregroundStyle(.secondary)
                                    }
                                }
                                Spacer()
                            }
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            .navigationTitle("Select Device")
            .listStyle(.insetGrouped)
            .background(Color(UIColor.systemGroupedBackground))
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button {
                        appState.manualShutdownTime = Date()
                        appState.showDeviceWifiSet = false
                    } label: {
                        Label("Cancel", systemImage: "xmark")
                    }
                }
            }
            .navigationDestination(for: BlufDeviceConfigPageType.self) { BlufDeviceConfigPageType in
                switch BlufDeviceConfigPageType {
                case .selectDevice:
                    SelectBlufiDevice()
                case .wifiConfig:
                    DeviceWifiConfig()
                }
            }
            .onAppear {
                BlufiUtil.shared.characteristicCallback = { characteristic in
                    // Check whether the characteristic is writable
                    if characteristic.properties.contains(.write) || characteristic.properties.contains(.writeWithoutResponse) {
                        if characteristic.uuid.uuidString == "E2E5E5E3-1234-5678-1234-56789ABCDEF0" {
                            BlufiUtil.shared.writeWifiSetCharacteristic = characteristic
                            self.path.append(.wifiConfig)
                        }
                    }
                }
            }
        }
    }
}

/// Wi-Fi configuration view
struct DeviceWifiConfig : View {
    
    enum Field {
        case Name
        case Password
    }
    
    @State private var wifiName: String = ""
    @State private var wifiPassword: String = ""
    
    @State private var locationManager = CLLocationManager()
    @State private var locationDelegate = LocationDelegate()
    
    @EnvironmentObject private var appState: AppState
    
    @FocusState private var focusedField: Field?
    
    @State private var showAlert: Bool = false
    @State private var alertMessage: String = ""
    
    @State private var title: String = "StackChan Wifi Setting"
    
    var body: some View {
        List {
            Section(header: Text("Name")) {
                TextField("Please enter the name of the wifi", text:$wifiName)
                    .focused($focusedField, equals: .Name)
                    .submitLabel(.next)
                    .onSubmit {
                        focusedField = .Password
                    }
            }
            Section(header: Text("Password")) {
                TextField("Please enter the password of the wifi", text:$wifiPassword)
                    .focused($focusedField, equals: .Password)
                    .submitLabel(.done)
                    .onSubmit {
                        confirmWifi()
                    }
            }
        }
        .listStyle(.insetGrouped)
        .background(Color(UIColor.systemGroupedBackground))
        .toolbar {
            ToolbarItem(placement: .confirmationAction) {
                Button {
                    confirmWifi()
                } label: {
                    Label("Submit", systemImage: "checkmark")
                }
            }
            ToolbarItem(placement: .cancellationAction) {
                Button {
                    appState.showDeviceWifiSet = false
                    BlufiUtil.shared.disconnectCurrentPeripheral()
                } label: {
                    Label("Cancel", systemImage: "xmark")
                }
            }
        }
        .alert(alertMessage, isPresented: $showAlert, actions: {
            Button("Confirm") {
                alertMessage = ""
                showAlert = false
            }
        })
        .navigationTitle(title)
        .onAppear {
            BlufiUtil.shared.wifiSetCharacteristicCall = { data in
                let json = data.hexEncodedString()
                
                print(data)
                
                if let model = BlufiModel<BlufiNotifyState>.fromJson(json), let state = model.data?.state {
                    if state == "wifiConnecting" {
                        // Configuring Wi-Fi
                        title = "In the configuration..."
                    } else if state == "wifiConnected" {
                        // Configuration succeeded
                        title = "Configuration successful"
                        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
                            appState.showDeviceWifiSet = false
                        }
                    } else if state == "wifiConnectFailed" {
                        // Configuration failed
                        title = "Configuration failed"
                        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
                            alertMessage = "Configuration failed, please re-enter wifi name and password"
                            showAlert = true
                            focusedField = .Password
                        }
                    }
                }
            }
            locationDelegate.onAuthorized = {
                getPermission()
            }
            locationManager.delegate = locationDelegate
            getPermission()
        }
        .onDisappear {
            locationManager.delegate = nil
        }
    }
    
    private func getWifiInfo() {
        NEHotspotNetwork.fetchCurrent { network in
            if let network = network {
                wifiName = network.ssid
                focusedField = .Password
            }
        }
    }
    
    private func confirmWifi() {
        if wifiName.isEmpty || wifiPassword.isEmpty {
            alertMessage = "Please enter the full name and password"
            showAlert = true
            return
        }
        
        let model = BlufiModel<BlufiWifi>(cmd: "setWifi",data: BlufiWifi(ssid: wifiName,password: wifiPassword))
        if let json = model.toJson() {
            BlufiUtil.shared.sendWifiSetData(json)
        }
    }
    
    private func getPermission() {
        if #available(iOS 14.0, *) {
            switch locationManager.authorizationStatus {
            case .authorizedWhenInUse, .authorizedAlways:
                getWifiInfo()
                break
            case .denied, .restricted:
                break
            case .notDetermined:
                locationManager.requestWhenInUseAuthorization()
                break
            default:
                break
            }
        } else {
            locationManager.requestWhenInUseAuthorization()
        }
    }
}

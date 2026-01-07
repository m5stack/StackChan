/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI
import NetworkExtension
import CoreLocation
import NetworkExtension
import CoreBluetooth

struct BindingDevice : View {
    
    enum BindingDevicePageType: Hashable {
        case scanningEquipment
    }
    
    @State private var path: [BindingDevicePageType] = []
    
    var body: some View {
        NavigationStack(path: $path) {
            VStack{
                Spacer()
                VStack(alignment: .leading, spacing: 16) {
                    
                    HStack {
                        Spacer()
                        Image("lateral_image")
                            .resizable()
                            .frame(maxWidth: 250,maxHeight: 250)
                        Spacer()
                    }
                    
                    Text("Get your StackChan device ready")
                        .font(.title2)
                        .bold()
                    
                    VStack(alignment: .leading, spacing: 12) {
                        HStack(alignment: .top) {
                            Image(systemName: "1.circle.fill")
                                .foregroundColor(.accentColor)
                            Text("Turn on your StackChan device")
                        }
                        HStack(alignment: .top) {
                            Image(systemName: "2.circle.fill")
                                .foregroundColor(.accentColor)
                            Text("After turning on the computer, turn the page to \"Setup\" and click to enter. A QR code will be displayed")
                        }
                        HStack(alignment: .top) {
                            Image(systemName: "3.circle.fill")
                                .foregroundColor(.accentColor)
                            Text("Align the QR code and scan it to bind the device")
                        }
                    }
                    .font(.body)
                }
                .padding()
                Spacer()
                NavigationLink(value: BindingDevicePageType.scanningEquipment) {
                    Text("Next")
                        .frame(maxWidth: .infinity)
                }
                .padding()
                .controlSize(.large)
                .buttonStyle(.borderedProminent)
            }
            .navigationTitle("Binding Device")
            .navigationDestination(for: BindingDevicePageType.self) { PageType in
                switch PageType {
                case .scanningEquipment:
                    ScanningEquipment()
                }
            }
        }
    }
}

struct ScanningEquipment : View {
    
    enum PairingStatus {
        case ScanCode
        case ConnectBlue
        case InputWiFi
        case DistributionNetwork
        case ChangeTheName
        case Empty
    }
    
    enum Field {
        case Name
        case Password
        case StackChanName
    }
    
    @EnvironmentObject var appState: AppState
    
    @State var pairingStatus: PairingStatus = .ScanCode
    
    @State var wifiName: String = ""
    @State var wifiPassword: String = ""
    @State var stackChanName: String = ""
    
    @State private var locationManager = CLLocationManager()
    @State private var locationDelegate = LocationDelegate()
    
    @FocusState private var focusedField: Field?
    
    var body: some View {
        Group {
            switch pairingStatus {
            case .ScanCode:
                GeometryReader { geometry in
                    ScanView { result in
                        switch result {
                        case .success(let data):
                            readCodeString(value: data)
                            break
                        case .failure(_):
                            break
                        }
                    }
                    .clipShape(
                        RoundedRectangle(
                            cornerRadius: min(geometry.size.width, geometry.size.height) * 0.1,
                            style: .continuous
                        )
                    )
                }
                .padding()
                .navigationTitle("Scan Device QR Code")
            case .ConnectBlue:
                VStack {
                    ProgressView()
                        .progressViewStyle(.circular)
                    Text("Connecting to Bluetooth")
                        .font(.title3)
                        .multilineTextAlignment(.center)
                }
                .frame(maxWidth: .infinity,maxHeight: .infinity, alignment: .center)
                .navigationTitle("Pairing devices")
            case .InputWiFi:
                VStack {
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
                    
                    Spacer()
                    
                    Button {
                        focusedField = nil
                        confirmWifi()
                    } label: {
                        Text("Confirm")
                            .frame(maxWidth: .infinity)
                    }
                    .padding()
                    .controlSize(.large)
                    .buttonStyle(.borderedProminent)
                }
                .background(Color(UIColor.systemGroupedBackground))
                .navigationTitle("Enter Wifi Information")
            case .DistributionNetwork:
                VStack {
                    ProgressView()
                        .progressViewStyle(.circular)
                    Text("The network is being configured for the equipment")
                        .font(.title3)
                        .multilineTextAlignment(.center)
                }
                .frame(maxWidth: .infinity,maxHeight: .infinity, alignment: .center)
                .navigationTitle("wait a moment")
            case .ChangeTheName:
                VStack {
                    List {
                        Section(header: Text("Name")) {
                            TextField("Please enter the name of the stackChan", text:$stackChanName)
                                .focused($focusedField, equals: .StackChanName)
                                .submitLabel(.done)
                                .onSubmit {
                                    updataName()
                                }
                        }
                    }
                    .listStyle(.insetGrouped)
                    
                    Spacer()
                    
                    Button {
                        focusedField = nil
                        updataName()
                    } label: {
                        Text("Confirm")
                            .frame(maxWidth: .infinity)
                    }
                    .padding()
                    .controlSize(.large)
                    .buttonStyle(.borderedProminent)
                }
                .frame(maxWidth: .infinity,maxHeight: .infinity, alignment: .center)
                .navigationTitle("Give me a name")
            default:
                EmptyView()
            }
        }
        .alert(appState.alertTitle, isPresented: $appState.showAlert){
            Button {
                appState.alertAction?()
            } label: {
                Text("Confirm")
            }
        }
        .task {
        }
    }
    
    func updataName() {
        
    }
    
    func readCodeString(value: String) {
        if let data = value.data(using: .utf8), let json = try? JSONSerialization.jsonObject(with: data) as? [String:Any], let mac = json["mac"] as? String {
            let extracted = mac
            let cleanedMac = extracted.uppercased().replacingOccurrences(
                of: "[^A-F0-9]",
                with: "",
                options: .regularExpression
            )
            appState.deviceMac = cleanedMac
            appState.showBindingDevice = false
            appState.connectWebSocket()
            appState.openBlufi()
        }
    }
    
    private func getBlueAndWifiInfo() {
        NEHotspotNetwork.fetchCurrent { network in
            if let network = network {
                wifiName = network.ssid
                focusedField = .Password
            }
        }
        BlufiUtil.shared.startScan()
    }
    
    private func getPermission() {
        if #available(iOS 14.0, *) {
            switch locationManager.authorizationStatus {
            case .authorizedWhenInUse, .authorizedAlways:
                getBlueAndWifiInfo()
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
    
    private func confirmWifi() {
        
        if !BlufiUtil.shared.blueSwitch {
            appState.alertTitle = "Please turn on Bluetooth"
            appState.showAlert = true
            return
        }
        
        if wifiName.isEmpty || wifiPassword.isEmpty {
            appState.alertTitle = "Please enter Wi-Fi name and password"
            appState.showAlert = true
            return
        }
        
        withAnimation{
            pairingStatus = .DistributionNetwork
        }
        
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
            withAnimation{
                pairingStatus = .ChangeTheName
            }
        }
    }
    
    private func hideKeyboard() {
        UIApplication.shared.sendAction(#selector(UIResponder.resignFirstResponder), to: nil, from: nil, for: nil)
    }
}


class LocationDelegate: NSObject,CLLocationManagerDelegate {
    var onAuthorized: (() -> Void)?
    
    func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        if manager.authorizationStatus == .authorizedWhenInUse || manager.authorizationStatus == .authorizedAlways {
            onAuthorized?()
        }
    }
}

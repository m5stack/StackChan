/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI

struct Settings : View {
    
    @EnvironmentObject var appState: AppState
    
    @State var showCjamgeNameAlert: Bool = false
    @State var newName: String = ""
    
    @State var deviceInfo: Device = Device()
    
    var body: some View {
        NavigationStack(path: $appState.settingsPath) {
            List {
                Section("conventional") {
                    Button {
                        if appState.deviceMac.isEmpty {
                            appState.showBindingDeviceAlert = true
                        } else {
                            appState.showCjamgeNameAlert = true
                        }
                    } label: {
                        HStack {
                            Image(systemName: "person")
                                .foregroundStyle(.white)
                                .frame(width: 28, height: 28)
                                .background(Color.blue)
                                .cornerRadius(8)
                            Text("Change Name")
                            Spacer()
                            Text(appState.deviceInfo.name ?? "")
                                .foregroundStyle(.secondary)
                            Image(systemName: "chevron.right")
                                .foregroundStyle(.secondary)
                        }
                    }
                    .foregroundStyle(.primary)
                    Button {
                    } label: {
                        HStack {
                            Image(systemName: "arrow.down.circle")
                                .foregroundStyle(.white)
                                .frame(width: 28, height: 28)
                                .background(Color.green)
                                .cornerRadius(8)
                            Text("Online upgrade")
                            Spacer()
                            Image(systemName: "chevron.right")
                                .foregroundStyle(.secondary)
                        }
                    }
                    .foregroundStyle(.primary)
                }
                
                Section("system") {
                    Button {
                    } label: {
                        HStack {
                            Image(systemName: "exclamationmark.arrow.triangle.2.circlepath")
                                .foregroundStyle(.white)
                                .frame(width: 28, height: 28)
                                .background(Color.red)
                                .cornerRadius(8)
                            Text("Factory data reset")
                            Spacer()
                            Image(systemName: "chevron.right")
                                .foregroundStyle(.secondary)
                        }
                    }.foregroundStyle(.primary)
                    Button {
                        appState.forcedDisplayBindingDevice = false
                        appState.showBindingDevice = true
                    } label: {
                        HStack {
                            Image(systemName: "shuffle")
                                .foregroundStyle(.white)
                                .frame(width: 28, height: 28)
                                .background(Color.blue)
                                .cornerRadius(8)
                            Text("Bind StachChan")
                            Spacer()
                            Image(systemName: "chevron.right")
                                .foregroundStyle(.secondary)
                        }
                    }
                    .foregroundStyle(.primary)
                }
            }
            .listStyle(.insetGrouped)
            .navigationTitle("Settings")
            .navigationDestination(for: PageType.self) { PageType in
                switch PageType {
                case .cameraPage:
                    CameraPage()
                case .minicryEmotion:
                    MimicryEmotion(deviceMac: $appState.deviceMac)
                case .dance:
                    Dance()
                }
            }
            .onAppear {
                appState.getDeviceInfo()
            }
        }
    }
}


struct SettingPreview : PreviewProvider {
    static var previews: some View {
        Settings()
    }
}

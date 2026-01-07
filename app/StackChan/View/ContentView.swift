/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI
import CoreBluetooth

struct ContentView: View {
    
    @EnvironmentObject var appState: AppState
    
    var body: some View {
        TabView {
            StackChan()
                .tabItem {
                    Label( "StackChan", systemImage: "ipod")
                }
            Nearby()
                .tabItem {
                    Label("Nearby", systemImage: "sensor")
                }
            Moments()
                .tabItem {
                    Label("Moments", systemImage: "person.3")
                }
            Settings()
                .tabItem {
                    Label("Settings", systemImage: "gear")
                }
        }
        .task {
            appState.openBlufi()
            if appState.deviceMac != "" {
                appState.connectWebSocket()
            }
        }
        .sheet(isPresented: $appState.showBindingDevice) {
            BindingDevice()
                .interactiveDismissDisabled(appState.forcedDisplayBindingDevice)
        }
        .sheet(isPresented: $appState.showDeviceWifiSet) {
            SelectBlufiDevice()
                .presentationDetents([.medium])
                .interactiveDismissDisabled(true)
        }
        .alert("Let's give the lovely StackChan a new name", isPresented: $appState.showCjamgeNameAlert, actions: {
            TextField("Please enter the name", text: $appState.newName)
            Button("Cancel", role: .cancel) {
                appState.showCjamgeNameAlert = false
            }
            Button("Confirm") {
                appState.showCjamgeNameAlert = false
                withAnimation {
                    appState.deviceInfo.name = appState.newName
                }
                appState.updateDeviceInfo()
            }
        })
        .alert("Please switch StackChan to the SETUP page, select \"App Bind Code\", and then switch to the settings page on the app to choose \"Bind Device\"", isPresented: $appState.showBindingDeviceAlert) {
            Button("Confirm") {
                appState.showBindingDeviceAlert = false
            }
        }
    }
}


struct ContentViewPreview : PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}

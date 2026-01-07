/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI
import MultipeerConnectivity
import CoreBluetooth

struct TextMessage : Codable {
    var name: String = ""
    var content: String = ""
}

struct Nearby: View {
    
    @State var deviceList: [DeviceInfo] = []
    
    @State var proxySize : CGSize = CGSize(width: 0, height: 0)
    
    @EnvironmentObject var appState: AppState
    
    @State var deviceMac: String = ""
    
    @State private var showCallPopup: Bool = false
    
    @State private var displayMode: Int = 1  // 1 star map mode, 2 list mode
    
    private let tag = "Nearby"
    @State private var callTitle: String = "Under request..."
    
    var body: some View {
        NavigationStack(path: $appState.nearbyPath) {
            ZStack {
                DazzlingBackground(backColors: [Color.accent.opacity(0.5), Color.pink.opacity(0.1),Color.blue.opacity(0.2)],background: Color(UIColor.systemBackground))
                    .ignoresSafeArea()
                
                if displayMode == 1 {
                    Canvas { context, size in
                        let center = CGPoint(x: size.width / 2, y: size.height / 2)
                        
                        for device in deviceList {
                            var path = Path()
                            path.move(to: center)
                            path.addLine(to: device.postion)
                            context.stroke(
                                path,
                                with: .color(.white),
                                lineWidth: 3
                            )
                        }
                    }
                    
                    GeometryReader { proxy in
                        Color.clear
                            .onAppear {
                                proxySize = proxy.size
                            }
                            .onChange(of: proxy.size) { newSize in
                                proxySize = newSize
                            }
                        ForEach(deviceList, id: \.device.mac) { device in
                            Menu {
                                Button {
                                    // Hi button logic
                                    let textMessage = TextMessage(name:"App",content: "👋")
                                    sendMessage(device: device,msgType: .textMessage,data: textMessage.toJsonString())
                                    launchAnimation(device: device, text: "👋")
                                } label: {
                                    Label("👋", systemImage: "hand.wave")
                                }
                                Button {
                                    // Heart button logic
                                    let textMessage = TextMessage(name:"App",content: "❤️")
                                    sendMessage(device: device,msgType: .textMessage,data: textMessage.toJsonString())
                                    launchAnimation(device: device, text:"❤️")
                                } label: {
                                    Label("❤️", systemImage: "heart.fill")
                                }
                                Button {
                                    // Video call button logic
                                    sendMessage(device: device,msgType: .requestCall, data: "")
                                    // Show request popup animation
                                    
                                } label: {
                                    Label("Video Call", systemImage: "video.fill")
                                }
                            } label: {
                                let name = (device.device.name?.isEmpty == false) ? device.device.name! : "StackChan"
                                AvatarView(name: name)
                                    .frame(width: 100)
                            }
                            .position(x: device.postion.x, y: device.postion.y)
                        }
                    }
                    RippleDiffusion {
                        AvatarView(name: appState.deviceInfo.name ?? "Me")
                    }
                    ForEach(flyingTexts) { flying in
                        Text(flying.text)
                            .font(.largeTitle)
                            .position(x: flying.start.x + (flying.end.x - flying.start.x) * flying.progress,
                                      y: flying.start.y + (flying.end.y - flying.start.y) * flying.progress
                            )
                            .opacity(1 - flying.progress)
                    }
                } else {
                    ScrollView {
                        LazyVStack(spacing: 12) {
                            ForEach(deviceList, id: \.device.mac) { device in
                                Menu {
                                    Button {
                                        // Hi button logic
                                        let textMessage = TextMessage(name:"App",content: "👋")
                                        sendMessage(device: device,msgType: .textMessage,data: textMessage.toJsonString())
                                        launchAnimation(device: device, text: "👋")
                                    } label: {
                                        Label("👋", systemImage: "hand.wave")
                                    }
                                    Button {
                                        // Heart button logic
                                        let textMessage = TextMessage(name:"App",content: "❤️")
                                        sendMessage(device: device,msgType: .textMessage,data: textMessage.toJsonString())
                                        launchAnimation(device: device, text:"❤️")
                                    } label: {
                                        Label("❤️", systemImage: "heart.fill")
                                    }
                                    Button {
                                        // Video call button logic
                                        sendMessage(device: device,msgType: .requestCall, data: "")
                                        // Show request popup animation
                                        
                                    } label: {
                                        Label("Video Call", systemImage: "video.fill")
                                    }
                                } label: {
                                    Text(device.device.name ?? "Unknown")
                                        .frame(maxWidth: .infinity)
                                        .padding(12)
                                        .glassEffectRegular(cornerRadius: 25)
                                }
                            }
                        }
                    }
                    .padding(12)
                }
            }
            .toolbar {
                ToolbarItem(placement: .primaryAction) {
                    Button {
                        if displayMode == 1 {
                            displayMode = 2
                        } else {
                            displayMode = 1
                        }
                    } label: {
                        Label {
                            Text("Display Mode")
                        } icon: {
                            Image(systemName: displayMode == 1 ? "circle.hexagonpath" : "list.bullet")
                        }
                    }
                }
            }
            .navigationTitle("Nearby")
            .navigationDestination(for: PageType.self) { PageType in
                switch PageType {
                case .cameraPage:
                    CameraPage()
                case .minicryEmotion:
                    MimicryEmotion(deviceMac: $deviceMac)
                case .dance:
                    Dance()
                }
            }
            .sheet(isPresented: $showCallPopup) {
                VStack(alignment:.center) {
                    Spacer()
                    Text(callTitle).font(.largeTitle)
                    Spacer()
                    HStack(alignment:.center) {
                        Spacer()
                        AvatarView(name: "Caller")
                        Spacer()
                        ProgressView()
                            .scaleEffect(1.5)
                        Spacer()
                        AvatarView(name: "Receiver")
                        Spacer()
                    }
                    Spacer()
                    Button {
                        showCallPopup = false
                        appState.sendWebSocketMessage(.hangupCall)
                    } label: {
                        VStack {
                            Color.clear.frame(
                                height: 15
                            )
                            Image(systemName: "phone.down.fill")
                                .font(.system(size: 50))
                                .frame(width: 100, height: 100)
                                .background(.red)
                                .clipShape(Circle())
                                .foregroundColor(.white)
                                .shadow(color: Color.gray, radius: 10, x: 0, y: 0)
                            Text("Hang up")
                                .frame(height: 15)
                                .foregroundColor(Color(UIColor.label))
                        }
                        .frame(width: 100)
                    }
                    Spacer()
                }
                .presentationDetents([.medium])
                .presentationBackgroundClear()
                .interactiveDismissDisabled(true)
            }
            .onAppear {
                //                bleInit()
                getDeviceList()
                WebSocketUtil.shared.addObserver(for: tag) { message in
                    switch message {
                    case .data(let data):
                        let result = appState.parseMessage(message: data)
                        if let msgType = result.0, let _ = result.1 {
                            switch msgType {
                            case MsgType.agreeCall:
                                // Agree to call
                                showCallPopup = false
                                DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
                                    appState.nearbyPath.append(.minicryEmotion)
                                }
                            case MsgType.refuseCall:
                                // Refuse call
                                callTitle = "The other party has refused."
                                DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
                                    showCallPopup = false
                                }
                            case MsgType.hangupCall:
                                showCallPopup = false
                                // Hang up call
                            default:
                                break
                            }
                        }
                    case .string(let text):
                        print("收到普通消息: \(text)")
                    @unknown default:
                        break
                    }
                }
                appState.getDeviceInfo()
            }
            .onDisappear {
                WebSocketUtil.shared.removeObserver(for: tag)
            }
        }
    }
    
    private func getDeviceList() {
        let map = [
            ValueConstant.mac: appState.deviceMac
        ]
        Networking.shared.get(pathUrl: Urls.deviceRandomList,parameters: map) { result in
            switch result {
            case .success(let success):
                do {
                    let response = try Response<[Device]>.decode(from: success)
                    if response.isSuccess, let list = response.data {
                        deviceList.removeAll()
                        for i in list {
                            let existingPositions = self.deviceList.map { $0.postion }
                            let newPosition = self.generateRandomPosition(existingPositions: existingPositions)
                            let newDevice = DeviceInfo(device: i, postion: newPosition)
                            withAnimation {
                                self.deviceList.append(newDevice)
                            }
                        }
                    }
                } catch {
                    print("Data parsing failed")
                }
            case .failure(let failure):
                print("Request failed:", failure)
            }
        }
    }
    
    @State private var flyingTexts: [FlyingText] = []
    
    private func launchAnimation(device: DeviceInfo, text: String) {
        let mineCenter = CGPoint(x: proxySize.width/2, y: proxySize.height/2)
        let targetCenter = device.postion
        let id = UUID()
        let flyingText = FlyingText(id: id, text: text, start: mineCenter, end: targetCenter, progress: 0)
        flyingTexts.append(flyingText)
        
        withAnimation(.linear(duration: 1.0)) {
            if let index = flyingTexts.firstIndex(where: { $0.id == id }) {
                flyingTexts[index].progress = 1
            }
        }
        
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0,) {
            flyingTexts.removeAll{ $0.id == id }
        }
    }
    
    private func sendMessage(device: DeviceInfo,msgType: MsgType, data: String) {
        if msgType == .requestCall {
            deviceMac = device.device.mac
            showCallPopup = true
            callTitle = "Under request..."
            // Automatically hang up after 20 seconds
            DispatchQueue.main.asyncAfter(deadline: .now() + 20.0) {
                if showCallPopup {
                    callTitle = "No one answered."
                    DispatchQueue.main.asyncAfter(deadline: .now() + 1.0,) {
                        showCallPopup = false
                        appState.sendWebSocketMessage(.hangupCall)
                    }
                }
            }
        }
        let dataString = device.device.mac + data
        appState.sendWebSocketMessage(msgType,dataString.toData())
    }
    
    struct FlyingText: Identifiable {
        let id: UUID
        let text: String
        let start: CGPoint
        let end: CGPoint
        var progress: CGFloat
    }
    
    private func bleInit() {
    }
    
    // Randomly generate a position, avoiding existing devices and the center area
    private func generateRandomPosition(existingPositions: [CGPoint]) -> CGPoint {
        let center = CGPoint(x: proxySize.width / 2, y: proxySize.height / 2)
        let selfSafeRadius: CGFloat = 100      // Avoid own avatar
        let otherSafeRadius: CGFloat = 100     // Avoid other devices
        let maxAttempts = 200                  // Increased number of attempts
        
        for _ in 0..<maxAttempts {
            let randomX = CGFloat.random(in: 50...(proxySize.width - 50))
            let randomY = CGFloat.random(in: 65...(proxySize.height - 65))
            let candidate = CGPoint(x: randomX, y: randomY)
            
            // Avoid own avatar
            if hypot(candidate.x - center.x, candidate.y - center.y) < selfSafeRadius {
                continue
            }
            // Avoid existing devices
            if existingPositions.allSatisfy({ hypot($0.x - candidate.x, $0.y - candidate.y) > otherSafeRadius }) {
                return candidate
            }
        }
        // If all attempts fail, randomly offset from the center to avoid overlap
        var offsetX = CGFloat.random(in: selfSafeRadius...(selfSafeRadius + 50))
        var offsetY = CGFloat.random(in: selfSafeRadius...(selfSafeRadius + 50))
        // Randomly determine direction
        offsetX *= Bool.random() ? 1 : -1
        offsetY *= Bool.random() ? 1 : -1
        return CGPoint(x: center.x + offsetX, y: center.y + offsetY)
    }
}

struct DeviceInfo {
    let device: Device
    let postion: CGPoint
}

struct AvatarView : View {
    
    let name: String
    
    var body: some View {
        VStack {
            Color.clear.frame(
                height: 15
            )
            Image("logo_icon")
                .resizable()
                .scaledToFit()
                .frame(width: 100, height: 100)
                .clipShape(Circle())
                .shadow(color: Color.gray, radius: 10, x: 0, y: 0)
            Text(name)
                .frame(height: 15)
                .foregroundColor(Color(UIColor.label))
        }
    }
}


struct NearbyPreview : PreviewProvider {
    static var previews: some View {
        Nearby()
    }
}

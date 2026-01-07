/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI

struct AvatarMotionControl : View {
    
    @State private var selectedItem: ControlItem = .avatar
    
    @EnvironmentObject var appState: AppState
    
    @State private var avatarData: ExpressionData = ExpressionData(leftEye: ExpressionItem(weight:100), rightEye: ExpressionItem(weight:100), mouth: ExpressionItem(weight:0))
    
    @State private var motionData: MotionData = MotionData(pitchServo: MotionDataItem(), yawServo: MotionDataItem())
    
    private let tag = "AvatarMotionControl"
    
    @State private var lastJoystickUpdate: Date = .distantPast
    
    enum ControlItem: String,CaseIterable, Identifiable {
        case avatar = "Avatar"
        case motion = "Motion"
        var id: String { rawValue }
    }
    
    var body: some View {
        VStack {
            HStack {
                Spacer()
                let danceData = DanceData(leftEye: avatarData.leftEye, rightEye: avatarData.rightEye, mouth: avatarData.mouth, yawServo: motionData.yawServo, pitchServo: motionData.pitchServo, durationMs: 1000)
                StackChanRobot(data: danceData)
                    .frame(height: 250)
                Spacer()
            }
            HStack {
                Picker("Select", selection: $selectedItem) {
                    ForEach(ControlItem.allCases) { item in
                        Text(item.rawValue)
                            .tag(item)
                    }
                }
                .pickerStyle(.segmented)
                Button {
                    if selectedItem == .avatar {
                        withAnimation {
                            avatarData = ExpressionData(leftEye: ExpressionItem(weight:100), rightEye: ExpressionItem(weight:100), mouth: ExpressionItem(weight:0))
                        }
                        saveAvatarData()
                    } else if selectedItem == .motion {
                        withAnimation {
                            motionData = MotionData(pitchServo: MotionDataItem(), yawServo: MotionDataItem())
                        }
                        saveMotionData()
                    }
                } label: {
                    Image(systemName: "arrow.counterclockwise")
                }
                .glassButtonStyle()
            }
            
            if selectedItem == .avatar {
                List {
                    Section("Left Eye") {
                        HStack {
                            Text("x")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(avatarData.leftEye.x) },
                                    set: { avatarData.leftEye.x = Int($0) }
                                ),
                                in: -100...100,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveAvatarData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(avatarData.leftEye.x))
                                .frame(width: 50,alignment: .trailing)
                        }
                        HStack {
                            Text("y")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(avatarData.leftEye.y) },
                                    set: { avatarData.leftEye.y = Int($0) }
                                ),
                                in: -100...100,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveAvatarData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(avatarData.leftEye.y))
                                .frame(width: 50,alignment: .trailing)
                        }
                        HStack {
                            Text("rotation")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(avatarData.leftEye.rotation) },
                                    set: { avatarData.leftEye.rotation = Int($0) }
                                ),
                                in: -1800...1800,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveAvatarData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(avatarData.leftEye.rotation))
                                .frame(width: 50,alignment: .trailing)
                        }
                        HStack {
                            Text("weight")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(avatarData.leftEye.weight) },
                                    set: { avatarData.leftEye.weight = Int($0) }
                                ),
                                in: 0...100,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveAvatarData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(avatarData.leftEye.weight))
                                .frame(width: 50,alignment: .trailing)
                        }
                        HStack {
                            Text("size")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(avatarData.leftEye.size) },
                                    set: { avatarData.leftEye.size = Int($0) }
                                ),
                                in: -100...100,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveAvatarData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(avatarData.leftEye.size))
                                .frame(width: 50,alignment: .trailing)
                        }
                    }
                    .listRowBackground(Color.clear)
                    
                    Section("Right Eye") {
                        HStack {
                            Text("x")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(avatarData.rightEye.x) },
                                    set: { avatarData.rightEye.x = Int($0) }
                                ),
                                in: -100...100,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveAvatarData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(avatarData.rightEye.x))
                                .frame(width: 50,alignment: .trailing)
                        }
                        HStack {
                            Text("y")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(avatarData.rightEye.y) },
                                    set: { avatarData.rightEye.y = Int($0) }
                                ),
                                in: -100...100,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveAvatarData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(avatarData.rightEye.y))
                                .frame(width: 50,alignment: .trailing)
                        }
                        HStack {
                            Text("rotation")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(avatarData.rightEye.rotation) },
                                    set: { avatarData.rightEye.rotation = Int($0) }
                                ),
                                in: -1800...1800,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveAvatarData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(avatarData.rightEye.rotation))
                                .frame(width: 50,alignment: .trailing)
                        }
                        HStack {
                            Text("weight")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(avatarData.rightEye.weight) },
                                    set: { avatarData.rightEye.weight = Int($0) }
                                ),
                                in: 0...100,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveAvatarData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(avatarData.rightEye.weight))
                                .frame(width: 50,alignment: .trailing)
                        }
                        HStack {
                            Text("size")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(avatarData.rightEye.size) },
                                    set: { avatarData.rightEye.size = Int($0) }
                                ),
                                in: -100...100,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveAvatarData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(avatarData.rightEye.size))
                                .frame(width: 50,alignment: .trailing)
                        }
                    }
                    .listRowBackground(Color.clear)
                    
                    Section("Mouth") {
                        HStack {
                            Text("x")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(avatarData.mouth.x) },
                                    set: { avatarData.mouth.x = Int($0) }
                                ),
                                in: -100...100,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveAvatarData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(avatarData.mouth.x))
                                .frame(width: 50,alignment: .trailing)
                        }
                        HStack {
                            Text("y")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(avatarData.mouth.y) },
                                    set: { avatarData.mouth.y = Int($0) }
                                ),
                                in: -100...100,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveAvatarData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(avatarData.mouth.y))
                                .frame(width: 50,alignment: .trailing)
                        }
                        HStack {
                            Text("rotation")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(avatarData.mouth.rotation) },
                                    set: { avatarData.mouth.rotation = Int($0) }
                                ),
                                in: -1800...1800,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveAvatarData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(avatarData.mouth.rotation))
                                .frame(width: 50,alignment: .trailing)
                        }
                        HStack {
                            Text("weight")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(avatarData.mouth.weight) },
                                    set: { avatarData.mouth.weight = Int($0) }
                                ),
                                in: 0...100,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveAvatarData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(avatarData.mouth.weight))
                                .frame(width: 50,alignment: .trailing)
                        }
                    }
                    .listRowBackground(Color.clear)
                }
                .listStyle(.grouped)
                .scrollContentBackground(.hidden)
                .background(.clear)
            } else if selectedItem == .motion {
                List {
                    Section("Joystick") {
                        HStack {
                            Spacer()
                            JoystickView { radians, strength in
                                if radians == 0 && strength == 0 {
                                    calculationJoystick(radians: radians, strength: strength)
                                } else {
                                    let now = Date()
                                    if now.timeIntervalSince(lastJoystickUpdate) > 0.1 {
                                        lastJoystickUpdate = now
                                        calculationJoystick(radians: radians, strength: strength)
                                    }
                                }
                            }
                            .frame(width: 200,height: 200)
                            Spacer()
                        }
                    }
                    .listRowBackground(Color.clear)
                    Section("Yaw Servo") {
                        HStack {
                            Text("angle")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(motionData.yawServo.angle) },
                                    set: {
                                        motionData.yawServo.rotate = Int(0)
                                        motionData.yawServo.angle = Int($0)
                                    }
                                ),
                                in: -1280...1280,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveMotionData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(motionData.yawServo.angle))
                                .frame(width: 50,alignment: .trailing)
                        }
                        HStack {
                            Text("speed")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(motionData.yawServo.speed) },
                                    set: { motionData.yawServo.speed = Int($0) }
                                ),
                                in: 0...1000,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveMotionData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(motionData.yawServo.speed))
                                .frame(width: 50,alignment: .trailing)
                        }
                        HStack {
                            Text("rotate")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(motionData.yawServo.rotate) },
                                    set: {
                                        motionData.yawServo.angle = Int(0)
                                        motionData.yawServo.rotate = Int($0)
                                    }
                                ),
                                in: -1000...1000,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveMotionData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(motionData.yawServo.rotate))
                                .frame(width: 50,alignment: .trailing)
                        }
                    }
                    .listRowBackground(Color.clear)
                    Section("Pitch Servo") {
                        HStack {
                            Text("angle")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(motionData.pitchServo.angle) },
                                    set: { motionData.pitchServo.angle = Int($0) }
                                ),
                                in: 0...900,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveMotionData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(motionData.pitchServo.angle))
                                .frame(width: 50,alignment: .trailing)
                        }
                        HStack {
                            Text("speed")
                                .frame(width: 60,alignment: .leading)
                            Slider(
                                value: Binding(
                                    get: { Double(motionData.pitchServo.speed) },
                                    set: { motionData.pitchServo.speed = Int($0) }
                                ),
                                in: 0...1000,
                                onEditingChanged: { editing in
                                    if !editing {
                                        saveMotionData()
                                    }
                                }
                            )
                            .frame(maxWidth: .infinity)
                            Text(String(motionData.pitchServo.speed))
                                .frame(width: 50,alignment: .trailing)
                        }
                    }
                    .listRowBackground(Color.clear)
                }
                .listStyle(.grouped)
                .scrollContentBackground(.hidden)
                .background(.clear)
            }
            Spacer()
        }
        .padding()
        .ignoresSafeArea()
        .onAppear {
            WebSocketUtil.shared.addObserver(for: tag) { message in
                switch message {
                case .data(let data):
                    let result = appState.parseMessage(message: data)
                    if let msgType = result.0, let parsedData = result.1 {
                        switch msgType {
                        case MsgType.getAvatarPosture:
                            print("Received the result of obtaining the header information" + String(parsedData.count))
                        default:
                            
                            break
                        }
                    }
                case .string(let text):
                    print("Received a regular message:" + text)
                @unknown default:
                    break
                }
            }
            appState.sendWebSocketMessage(.getAvatarPosture)
        }
        .onDisappear {
            WebSocketUtil.shared.removeObserver(for: tag)
        }
    }
    
    /// Calculate the joystick data
    private func calculationJoystick(radians: CGFloat, strength: CGFloat) {
        let x = strength * cos(radians)
        let y = strength * sin(radians)
        let deadZone: CGFloat = 0.0
        var yawValue: Int = 0
        if abs(x) > deadZone {
            yawValue = Int(x * 1280)
            motionData.yawServo.rotate = 0
            motionData.yawServo.angle = yawValue
            motionData.yawServo.speed = 50
        } else {
            motionData.yawServo.rotate = 0
            motionData.yawServo.angle = 0
            motionData.yawServo.speed = 500
        }
        if y <= 0 {
            if abs(y) > deadZone {
                let normalizedY = max(-y, 0)
                let newPitch = Int(normalizedY * 900)
                motionData.pitchServo.angle = newPitch
                motionData.pitchServo.speed = 50
            } else {
                motionData.pitchServo.speed = 500
                motionData.pitchServo.angle = 0
            }
        } else {
            motionData.pitchServo.speed = 500
            motionData.pitchServo.angle = 0
        }
        saveMotionData()
    }
    
    private func saveAvatarData() {
        if !appState.deviceMac.isEmpty {
            let jsonString = appState.deviceMac + avatarData.toJsonString()
            let data = jsonString.toData()
            appState.sendWebSocketMessage(.controlAvatar, data)
        }
    }
    
    private func saveMotionData() {
        if !appState.deviceMac.isEmpty {
            let jsonString = appState.deviceMac + motionData.toJsonString()
            print(jsonString)
            
            let data = jsonString.toData()
            appState.sendWebSocketMessage(.controlMotion, data)
        }
    }
}


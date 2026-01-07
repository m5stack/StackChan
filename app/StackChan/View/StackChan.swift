/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI

struct StackChan : View {
    
    let gridHeight: CGFloat = 100
    
    @EnvironmentObject var appState: AppState
    
    private let imageSize: CGFloat = 200
    
    @State private var showAvatarMotionControl: Bool = false
    
    @State private var deviceMac: String = ""
    
    func getDeviceStatus() -> String {
        if appState.deviceMac == "" {
            return "Unbound device"
        } else {
            if appState.deviceIsOnline {
                return "Device Online"
            } else {
                return "Device Offline"
            }
        }
    }
    
    var body: some View {
        let radius = UIScreen.main.bounds.minDimension / 12
        NavigationStack(path: $appState.stackChanPath) {
            ZStack {
                LinearGradient(
                    colors: [
                        Color.accent.opacity(0.5),
                        Color.pink.opacity(0.1),
                        Color.blue.opacity(0.2)
                    ],
                    startPoint: .topLeading,
                    endPoint: .bottomTrailing
                )
                .ignoresSafeArea()
                ScrollView {
                    VStack(alignment: .trailing,spacing: 20) {
                        StackChanRotaryRobot()
                            .frame(width: imageSize,height: imageSize)
                        Text(getDeviceStatus())
                        Button {
                            if appState.deviceMac.isEmpty {
                                appState.showBindingDeviceAlert = true
                            } else {
                                appState.stackChanPath.append(.minicryEmotion)
                            }
                        } label: {
                            HStack {
                                Image(systemName: "face.smiling")
                                    .font(.system(size: 44))
                                Spacer()
                                Text("AVATAR")
                                    .font(.largeTitle)
                            }
                            .padding(.horizontal,20)
                            .foregroundColor(Color(UIColor.systemBackground))
                            .padding()
                            .frame(maxWidth: .infinity)
                            .frame(height: gridHeight)
                            .background(.yellow)
                            .clipShape(RoundedRectangle(cornerRadius: radius))
                            .glassEffectRegular(cornerRadius: radius)
                        }
                        
                        Button {
                            if appState.deviceMac.isEmpty {
                                appState.showBindingDeviceAlert = true
                            } else {
                                appState.stackChanPath.append(.cameraPage)
                            }
                        } label: {
                            HStack {
                                Image(systemName: "video")
                                    .font(.system(size: 44))
                                Spacer()
                                Text("SENTINEL")
                                    .font(.largeTitle)
                            }
                            .padding(.horizontal,20)
                            .foregroundColor(Color(UIColor.systemBackground))
                            .padding()
                            .frame(maxWidth: .infinity)
                            .frame(height: gridHeight)
                            .background(Color(UIColor.label).opacity(0.8))
                            .clipShape(RoundedRectangle(cornerRadius: radius))
                            .glassEffectRegular(cornerRadius: radius)
                        }
                        
                        Button {
                            if appState.deviceMac.isEmpty {
                                appState.showBindingDeviceAlert = true
                            } else {
                                showAvatarMotionControl = true
                            }
                        } label: {
                            HStack {
                                Image(
                                    systemName: "arrow.up.and.down.and.arrow.left.and.right"
                                )
                                .font(.system(size: 44))
                                Spacer()
                                Text("MOTION")
                                    .font(.largeTitle)
                            }
                            .padding(.horizontal,20)
                            .foregroundColor(Color(UIColor.label))
                            .padding()
                            .frame(maxWidth: .infinity)
                            .frame(height: gridHeight)
                            .background(.gray.opacity(0.5))
                            .clipShape(RoundedRectangle(cornerRadius: radius))
                            .glassEffectRegular(cornerRadius: radius)
                        }
                        
                        Button {
                            if appState.deviceMac.isEmpty {
                                appState.showBindingDeviceAlert = true
                            } else {
                                appState.stackChanPath.append(.dance)
                            }
                        } label: {
                            HStack {
                                Image(systemName: "figure.dance")
                                    .font(.system(size: 44))
                                Spacer()
                                Text("DANCE")
                                    .font(.largeTitle)
                            }
                            .padding(.horizontal,20)
                            .foregroundColor(Color(UIColor.systemBackground))
                            .padding()
                            .frame(maxWidth: .infinity)
                            .frame(height: gridHeight)
                            .background(.orange)
                            .clipShape(RoundedRectangle(cornerRadius: radius))
                            .glassEffectRegular(cornerRadius: radius)
                        }
                    }
                    .padding(20)
                }
            }
            .sheet(isPresented: $showAvatarMotionControl) {
                AvatarMotionControl()
                    .presentationDetents([.medium,.large])
                    .presentationBackgroundClear()
                    .presentationDragIndicator(.visible)
            }
            .sheet(isPresented: $appState.showSwitchFace) {
                SelectBlufiDevice()
                    .presentationDetents([.medium])
                    .presentationBackgroundClear()
            }
            .navigationTitle("StackChan")
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
        }
    }
}



struct SwitchFacePreview : PreviewProvider {
    static var previews: some View {
        SwitchFace()
    }
}



struct SwitchFace : View {
    
    private let faceList: [ExpressionData] = [
        ExpressionData(
            leftEye: ExpressionItem(
                x: 0,
                y: -50,
                rotation: 1400,
                weight: 60,
                size: 0
            ),
            rightEye: ExpressionItem(
                x: 0,
                y: -50,
                rotation: -1400,
                weight: 60,
                size: 0
            ),
            mouth: ExpressionItem(x: 0, y: 0, rotation: 0, weight: 50, size: 0)
        ),
        ExpressionData(
            leftEye: ExpressionItem(
                x: 0,
                y: 0,
                rotation: 0,
                weight: 100,
                size: 0
            ),
            rightEye: ExpressionItem(
                x: 0,
                y: 0,
                rotation: 0,
                weight: 100,
                size: 0
            ),
            mouth: ExpressionItem(x: 0, y: 0, rotation: 0, weight: 0, size: 0)
        ),
        ExpressionData(
            leftEye: ExpressionItem(
                x: 0,
                y: 0,
                rotation: 250,
                weight: 50,
                size: 0
            ),
            rightEye: ExpressionItem(
                x: 0,
                y: 0,
                rotation: -250,
                weight: 50,
                size: 0
            ),
            mouth: ExpressionItem(x: 0, y: 0, rotation: 0, weight: 0, size: 0)
        ),
        ExpressionData(
            leftEye: ExpressionItem(
                x: 0,
                y: 0,
                rotation: 0,
                weight: 15,
                size: 0
            ),
            rightEye: ExpressionItem(
                x: 0,
                y: 0,
                rotation: 0,
                weight: 15,
                size: 0
            ),
            mouth: ExpressionItem(x: 0, y: 0, rotation: 0, weight: 0, size: 0)
        ),
        ExpressionData(
            leftEye: ExpressionItem(
                x: 0,
                y: 0,
                rotation: 0,
                weight: 100,
                size: 50
            ),
            rightEye: ExpressionItem(
                x: 5,
                y: 0,
                rotation: 0,
                weight: 100,
                size: -50
            ),
            mouth: ExpressionItem(x: 0, y: 0, rotation: 0, weight: 0, size: 0)
        ),
        ExpressionData(
            leftEye: ExpressionItem(
                x: 0,
                y: -50,
                rotation: 0,
                weight: 100,
                size: 50
            ),
            rightEye: ExpressionItem(
                x: 0,
                y: -50,
                rotation: 0,
                weight: 100,
                size: 50
            ),
            mouth: ExpressionItem(x: 0, y: 0, rotation: 0, weight: 100, size: 0)
        )
    ]
    
    @State var selectedIndex: Int = 0
    
    private let columns = Array(repeating: GridItem(.flexible(),spacing: 20), count: 2)
    
    @EnvironmentObject var appState: AppState
    
    var body: some View {
        ScrollView {
            LazyVGrid(columns: columns, spacing: 20) {
                ForEach(0..<faceList.count, id: \.self) { index in
                    FaceCell(expression: faceList[index], isSelected: selectedIndex == index)
                        .onTapGesture {
                            withAnimation(.easeInOut(duration: 0.2)) {
                                selectedIndex = index
                            }
                            let jsonString = appState.deviceMac + faceList[index].toJsonString()
                            let data = jsonString.toData()
                            appState.sendWebSocketMessage(.controlAvatar, data)
                        }
                }
            }
            .padding(20)
        }
        .ignoresSafeArea()
    }
}

struct FaceCell: View {
    let expression: ExpressionData
    let isSelected: Bool
    let expressionLayer: ExpressionLayer
    
    init(expression: ExpressionData, isSelected: Bool) {
        self.expression = expression
        self.isSelected = isSelected
        self.expressionLayer = ExpressionLayer(data: expression)
        self.expressionLayer.frame = CGRect(origin: .zero, size: CGSize(width: 320, height: 240))
        self.expressionLayer.setNeedsDisplay()
    }
    var body: some View {
        let newImage = expressionRenderer().image { ctx in
            self.expressionLayer.render(in: ctx.cgContext)
        }
        Image(uiImage: newImage)
            .resizable()
            .aspectRatio(4/3, contentMode: .fit)
            .clipShape(RoundedRectangle(cornerRadius: 12))
            .overlay(
                RoundedRectangle(cornerRadius: 12)
                    .stroke(isSelected ? Color.blue : Color.gray.opacity(0.3),
                            lineWidth: isSelected ? 3 : 1)
            )
            .scaleEffect(isSelected ? 1.05 : 1.0)
    }
    
    private func expressionRenderer() -> UIGraphicsImageRenderer {
        let format = UIGraphicsImageRendererFormat.default()
        format.scale = UIScreen.main.scale
        format.opaque = false
        return UIGraphicsImageRenderer(
            size: expressionLayer.bounds.size,
            format: format
        )
    }
}

struct StackChanSwitchFacePreview : PreviewProvider {
    static var previews: some View {
        SwitchFace()
    }
}



/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI

struct CameraPage: View {
    
    @State var showControlButton = true
    @State var fullScreenDisplay = false
    
    @State var controlWidth: CGFloat = 0
    @State var controlHeight: CGFloat = 0
    
    private let controlButtonSize: CGFloat = 88
    private let bottomButtonSize: CGFloat = 50
    
    @State var openMicrophone : Bool = false
    @State var openSpeaker: Bool = true
    @State var startRecord: Bool = false
    @State var isPress: Int? = nil
    
    @EnvironmentObject var appState: AppState
    
    @AppStorage("recordMotion") private var recordMotionData: Data = Data()
    
    @State var recordMotion: [MotionData] = []
    
    private let feedback = UIImpactFeedbackGenerator(style: .medium)
    
    @State private var removeRecordPoint: Bool = false
    
    @State private var cameraImage: Data? = nil
    
    @State var showAlert = false
    @State var alertMessage = ""
    
    private let tag: String = "CameraPage"
    
    @State private var motionData: MotionData = MotionData(pitchServo: MotionDataItem(), yawServo: MotionDataItem())
    
    @State private var pressTimer: Timer? = nil
    
    private let longPressInterval = 0.05
    private let longStepValue = 50
    
    var body: some View {
        GeometryReader { geo in
            if fullScreenDisplay {
                VStack {
                    cameraView()
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
                .toolbar(.hidden, for: .bottomBar,.navigationBar,.tabBar,.automatic)
                .ignoresSafeArea(.all)
                .background(.black)
            } else {
                let isPortrait = geo.size.height > geo.size.width
                ZStack(alignment: isPortrait ? .bottomTrailing : .topTrailing) {
                    if isPortrait {
                        VStack {
                            cameraView()
                                .frame(maxWidth: .infinity, maxHeight: .infinity)
                            cameraControl(isPortrait: isPortrait)
                                .frame(maxWidth: .infinity,maxHeight: controlHeight)
                                .opacity(showControlButton ? 1: 0)
                        }
                    } else {
                        HStack {
                            cameraView()
                                .frame(maxWidth: .infinity, maxHeight: .infinity)
                            cameraControl(isPortrait: isPortrait)
                                .frame(maxWidth: controlWidth, maxHeight: .infinity)
                                .opacity(showControlButton ? 1: 0)
                        }
                    }
                    
                    HStack {
                        Spacer()
                        Button {
                            withAnimation(.easeInOut(duration: 0.3)) {
                                showControlButton.toggle()
                                updateControlSize(isPortrait: isPortrait, size: geo.size)
                            }
                        } label: {
                            Image(systemName:
                                    showControlButton
                                  ? (isPortrait ? "chevron.down" : "chevron.right")
                                  : (isPortrait ? "chevron.up" : "chevron.left")
                            )
                            .frame(width: bottomButtonSize, height: bottomButtonSize)
                            .font(.system(size: bottomButtonSize / 2))
                        }
                        .glassEffectCircle()
                    }
                    .padding(12)
                }
                .padding(0)
                .onAppear {
                    DispatchQueue.main.async {
                        if controlWidth == 0 && controlHeight == 0 {
                            controlHeight = geo.size.height / 2
                        }
                    }
                    WebSocketUtil.shared.addObserver(for: tag) { (message: URLSessionWebSocketTask.Message) in
                        switch message {
                        case .data(let data):
                            let result = appState.parseMessage(message: data)
                            if let msgType = result.0, let parsedData = result.1 {
                                switch msgType {
                                case MsgType.jpeg:
                                    cameraImage = parsedData
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
                    //on device camera
                    appState.sendWebSocketMessage(.onCamera,appState.deviceMac.toData())
                }
                .onDisappear {
                    WebSocketUtil.shared.removeObserver(for: tag)
                    //off device camera
                    appState.sendWebSocketMessage(.offCamera,appState.deviceMac.toData())
                }
                .onChange(of: geo.size) { newValue in
                    let isPortrait = newValue.height > newValue.width
                    updateControlSize(isPortrait: isPortrait, size: newValue)
                }
                .toolbar(.hidden, for: .tabBar)
                .navigationTitle("SENTINEL")
            }
        }
        .alert(alertMessage, isPresented: $showAlert) {
            Button {
                
            } label: {
                Text("Confirm")
            }
        }
        .onAppear {
            recordMotion = getRecordMotion()
        }
    }
    
    private func getRecordMotion() -> [MotionData] {
        guard let decoded = try? JSONDecoder().decode([MotionData].self, from: recordMotionData) else {
            return []
        }
        return decoded
    }
    
    private func setRecordMotion(_ newValue: [MotionData]) {
        if let encoded = try? JSONEncoder().encode(newValue) {
            recordMotionData = encoded
            recordMotion = newValue
        }
    }
    
    private func updateControlSize(isPortrait: Bool, size: CGSize) {
        withAnimation {
            if showControlButton {
                if isPortrait {
                    controlHeight = size.height / 2
                } else {
                    controlWidth = size.width / 2
                }
            } else {
                if isPortrait {
                    controlHeight = 0
                } else {
                    controlWidth = 0
                }
            }
        }
    }
    
    private func cameraView() -> some View {
        GeometryReader { geo in
            VStack {
                Spacer()
                ZStack(alignment: .bottom) {
                    if let cameraData = cameraImage, let uiImage = UIImage(data: cameraData) {
                        Image(uiImage: uiImage)
                            .resizable()
                            .scaledToFit()
                    } else {
                        Color.gray
                    }
                    HStack {
                        Spacer()
                        Button {
                            withAnimation {
                                fullScreenDisplay.toggle()
                            }
                        } label: {
                            Image(systemName: fullScreenDisplay ? "arrow.down.right.and.arrow.up.left" : "arrow.up.left.and.arrow.down.right")
                                .frame(width: 44,height: 44)
                                .foregroundColor(.white)
                        }
                        .glassEffectCircle()
                    }
                    .padding()
                }
                .frame(
                    maxWidth: geo.size.width,
                    maxHeight: min(geo.size.height, geo.size.width * 3 / 4)
                )
                Spacer()
            }
        }
    }
    
    @ViewBuilder private func cameraRecordPoint() -> some View {
        ZStack {
            ScrollViewReader { proxy in
                ScrollView(.vertical, showsIndicators: true) {
                    VStack(spacing: 5) {
                        Color.clear.frame(width: bottomButtonSize / 2,height: bottomButtonSize / 2)
                        ForEach(Array(recordMotion.indices), id: \.self) { index in
                            ZStack(alignment: .topTrailing) {
                                Button {
                                    if removeRecordPoint {
                                        recordMotion.remove(at: index)
                                        setRecordMotion(recordMotion)
                                    } else {
                                        feedback.impactOccurred()
                                        motionData = recordMotion[index]
                                        saveMotionData()
                                    }
                                } label: {
                                    Text(String(index + 1))
                                        .frame(width: bottomButtonSize, height: bottomButtonSize)
                                        .background(
                                            Circle()
                                                .fill(Color(UIColor.secondarySystemFill))
                                        )
                                        .font(.system(size: bottomButtonSize / 2))
                                }
                                if removeRecordPoint {
                                    Image(systemName: "minus.circle.fill")
                                        .font(.system(size: 20))
                                        .foregroundColor(.red)
                                }
                            }
                        }
                        Color.clear.frame(width: bottomButtonSize / 2,height: bottomButtonSize / 2).id(-1)
                    }
                    .padding(0)
                }
                .padding(0)
                .onChange(of: recordMotion.count) { _ in
                    withAnimation {
                        proxy.scrollTo(-1, anchor: .bottom)
                    }
                }
            }
            .padding(.vertical, bottomButtonSize / 2)
            
            VStack {
                Button {
                    withAnimation {
                        recordMotion.append(motionData)
                        setRecordMotion(recordMotion)
                    }
                } label: {
                    Image(systemName: "plus")
                        .frame(width: bottomButtonSize, height: bottomButtonSize)
                        .font(.system(size: bottomButtonSize / 2))
                }
                .glassEffectCircle()
                
                Spacer()
                
                Button {
                    withAnimation {
                        removeRecordPoint.toggle()
                    }
                } label: {
                    Image(systemName: removeRecordPoint ? "checkmark" : "minus")
                        .frame(width: bottomButtonSize, height: bottomButtonSize)
                        .foregroundColor(removeRecordPoint ? .accent : Color(UIColor.label))
                        .font(.system(size: bottomButtonSize / 2))
                }
                .glassEffectCircle()
            }
            .padding(0)
        }.background(
            RoundedRectangle(cornerRadius: 50)
                .fill(.ultraThinMaterial)
        )
    }
    
    @ViewBuilder private func cameraControl(isPortrait: Bool) -> some View {
        
        let microphone = Button {
            feedback.impactOccurred()
            withAnimation {
                openMicrophone.toggle()
            }
        } label: {
            Image(systemName: openMicrophone ? "microphone" : "microphone.slash")
                .frame(width: bottomButtonSize, height: bottomButtonSize)
                .font(.system(size: bottomButtonSize / 2))
        }
            .glassEffectCircle()
        
        let speaker = Button {
            feedback.impactOccurred()
            withAnimation {
                openSpeaker.toggle()
            }
        } label: {
            Image(systemName: openSpeaker ? "speaker" : "speaker.slash")
                .frame(width: bottomButtonSize, height: bottomButtonSize)
                .font(.system(size: bottomButtonSize / 2))
        }
            .glassEffectCircle()
        
        let record = Button {
            feedback.impactOccurred()
            withAnimation {
                startRecord.toggle()
            }
        } label: {
            Image(systemName: !startRecord ? "record.circle" : "record.circle.fill")
                .frame(width: bottomButtonSize, height: bottomButtonSize)
                .font(.system(size: bottomButtonSize / 2))
        }
            .glassEffectCircle()
            .foregroundColor(!startRecord ? Color(UIColor.label) : .red)
        
        let directionButton = ZStack{
            Button(action: {
                if let messsage = "Hi".toData() {
                    appState.sendWebSocketMessage(.onCamera, messsage)
                }
            }) {
                Image(systemName: "arrow.up")
                    .frame(width: controlButtonSize, height: controlButtonSize)
                    .background(
                        Circle().fill(.ultraThinMaterial)
                            .overlay(
                                Circle()
                                    .fill(isPress == 1 ? Color.accent : .clear)
                            )
                    )
                    .foregroundColor(isPress == 1 ? .white : Color(UIColor.label))
                    .font(.system(size: controlButtonSize / 2))
            }
            .buttonStyle(.plain)
            .simultaneousGesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in
                        if isPress != nil {
                            return
                        }
                        withAnimation {
                            isPress = 1
                        }
                        feedback.impactOccurred()
                        print("Up pressed")
                        
                        pressTimer?.invalidate()
                        pressTimer = Timer.scheduledTimer(withTimeInterval: longPressInterval, repeats: true, block: { _ in
                            self.motionData.pitchServo.angle += longStepValue
                            self.saveMotionData()
                        })
                    }
                    .onEnded { _ in
                        isPress = nil
                        
                        pressTimer?.invalidate()
                        pressTimer = nil
                    }
            )
            .offset(x: 0, y: -(controlButtonSize / 1.3))
            
            Button(action: {}) {
                Image(systemName: "arrow.down")
                    .frame(width: controlButtonSize, height: controlButtonSize)
                    .background(
                        Circle().fill(.ultraThinMaterial)
                            .overlay(
                                Circle()
                                    .fill(isPress == 3 ? Color.accent : .clear)
                            )
                    )
                    .foregroundColor(isPress == 3 ? .white : Color(UIColor.label))
                    .font(.system(size: controlButtonSize / 2))
            }
            .buttonStyle(.plain)
            .simultaneousGesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in
                        if isPress != nil {
                            return
                        }
                        withAnimation {
                            isPress = 3
                        }
                        feedback.impactOccurred()
                        
                        pressTimer?.invalidate()
                        pressTimer = Timer.scheduledTimer(withTimeInterval: longPressInterval, repeats: true, block: { _ in
                            self.motionData.pitchServo.angle -= longStepValue
                            self.saveMotionData()
                        })
                    }
                    .onEnded { _ in
                        isPress = nil
                        
                        pressTimer?.invalidate()
                        pressTimer = nil
                    }
            )
            .offset(x: 0, y: (controlButtonSize / 1.3))
            
            Button(action: {}) {
                Image(systemName: "arrow.left")
                    .frame(width: controlButtonSize, height: controlButtonSize)
                    .background(
                        Circle().fill(.ultraThinMaterial)
                            .overlay(
                                Circle()
                                    .fill(isPress == 4 ? Color.accent : .clear)
                            )
                    )
                    .foregroundColor(isPress == 4 ? .white : Color(UIColor.label))
                    .font(.system(size: controlButtonSize / 2))
            }
            .buttonStyle(.plain)
            .simultaneousGesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in
                        if isPress != nil {
                            return
                        }
                        withAnimation {
                            isPress = 4
                        }
                        feedback.impactOccurred()
                        
                        pressTimer?.invalidate()
                        pressTimer = Timer.scheduledTimer(withTimeInterval: longPressInterval, repeats: true, block: { _ in
                            self.motionData.yawServo.angle -= longStepValue
                            self.saveMotionData()
                        })
                    }
                    .onEnded { _ in
                        isPress = nil
                        
                        pressTimer?.invalidate()
                        pressTimer = nil
                    }
            )
            .offset(x: -(controlButtonSize / 1.3), y: 0)
            
            Button(action: {}) {
                Image(systemName: "arrow.right")
                    .frame(width: controlButtonSize, height: controlButtonSize)
                    .background(
                        Circle().fill(.ultraThinMaterial)
                            .overlay(
                                Circle()
                                    .fill(isPress == 2 ? Color.accent : .clear)
                            )
                    )
                    .foregroundColor(isPress == 2 ? .white : Color(UIColor.label))
                    .font(.system(size: controlButtonSize / 2))
            }
            .buttonStyle(.plain)
            .simultaneousGesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in
                        if isPress != nil {
                            return
                        }
                        withAnimation {
                            isPress = 2
                        }
                        feedback.impactOccurred()
                        
                        pressTimer?.invalidate()
                        pressTimer = Timer.scheduledTimer(withTimeInterval: longPressInterval, repeats: true, block: { _ in
                            self.motionData.yawServo.angle += longStepValue
                            self.saveMotionData()
                        })
                    }
                    .onEnded { _ in
                        isPress = nil
                        
                        pressTimer?.invalidate()
                        pressTimer = nil
                    }
            )
            .offset(x: (controlButtonSize / 1.3), y: 0)
        }
        
        if isPortrait {
            VStack {
                Spacer()
                HStack {
                    VStack {
                        Text("View\nPresets").font(.caption).multilineTextAlignment(.center)
                        cameraRecordPoint()
                    }
                    Spacer()
                    directionButton
                    Spacer()
                }
                .padding(0)
                Spacer()
                HStack {
                    record
                    Spacer()
                    speaker
                    microphone
                    Color.clear.frame(width: bottomButtonSize,height: bottomButtonSize)
                }
                .padding(0)
            }
            .padding(12)
            .foregroundColor(Color(UIColor.label))
        } else {
            HStack {
                HStack {
                    VStack {
                        Text("View\nPresets").font(.caption).multilineTextAlignment(.center)
                        cameraRecordPoint()
                    }
                    Spacer()
                    directionButton
                    Spacer()
                }
                Spacer()
                VStack {
                    Color.clear.frame(width: bottomButtonSize,height: bottomButtonSize)
                    speaker
                    microphone
                    Spacer()
                    record
                }
                .padding(0)
            }
            .padding(12)
            .foregroundColor(Color(UIColor.label))
        }
    }
    
    private func saveMotionData() {
        if !appState.deviceMac.isEmpty {
            let jsonString = appState.deviceMac + motionData.toJsonString()
            let data = jsonString.toData()
            appState.sendWebSocketMessage(.controlMotion, data)
        }
    }
}

struct CameraPageViewPreview : PreviewProvider {
    static var previews: some View {
        CameraPage()
    }
}

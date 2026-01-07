/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI
import AVFoundation
import ARKit

struct MimicryEmotion: View {
    
    @State var microphone: Bool = false // Whether the microphone is enabled
    @State var emotions: [String] = ["Smile"] // Commonly detected emotions
    
    @State var expressionData: ExpressionData = ExpressionData(leftEye: ExpressionItem(), rightEye: ExpressionItem(), mouth: ExpressionItem())
    @State var headData: MotionData = MotionData(pitchServo: MotionDataItem(), yawServo: MotionDataItem())
    
    @State private var lastSendTime: Date = Date(timeIntervalSince1970: 0)
    
    @State private var volume: CGFloat = 0
    
    @State var cameraImage: Data = Data()
    
    // Emotion detection threshold configuration
    private let emotionThresholds = EmotionThresholds()
    
    private let feedback = UIImpactFeedbackGenerator(style: .medium)
    
    @EnvironmentObject var appState: AppState
    
    private let tag = "MimicryEmotion"
    
    @Binding var deviceMac: String
    
    @Environment(\.dismiss) var dismiss
    
    @State var decorate: Int = 1 // Decoration: 0 = none, 1 = StackChan, 2 = pig nose
    
    @State var showPhoneScreen: Bool  = false // Whether to display phone screen on StackChan
    
    private let stackChanTargetSize = CGSize(width: 320, height: 240)
    
    var body: some View {
        ZStack {
            // Face camera preview
            VStack(spacing: 0) {
                if let uiImage = UIImage(data: cameraImage) {
                    Image(uiImage: uiImage)
                        .resizable()
                        .scaledToFit()
                        .frame(maxWidth: .infinity)
                } else {
                    Color.black.aspectRatio(4/3, contentMode: .fit)
                }
                ARCameraView(expressionData: $expressionData, decorate: $decorate, captureScreen: $showPhoneScreen, onCallback: { session, anchors in
                    DispatchQueue.main.async {
                        emotionDetection(session: session, anchors: anchors)
                    }
                }, onFrameCallback: { image in
                    compressMobilePhoneScreen(image: image)
                })
                .frame(maxWidth: .infinity,maxHeight: .infinity)
            }
            .ignoresSafeArea()
            
            VStack {
                
                HStack {
                    Spacer()
                }
                
                Spacer()
                
                HStack {
                    Button {
                        withAnimation {
                            if decorate == 0 {
                                decorate = 1
                            } else if decorate == 1 {
                                decorate = 2
                            } else if decorate == 2 {
                                decorate = 0
                            }
                        }
                        feedback.impactOccurred()
                    } label: {
                        switch decorate {
                        case 0:
                            Image(systemName: "slash.circle")
                                .frame(width: 88, height: 88)
                                .font(.system(size: 44))
                                .foregroundStyle(.white)
                        case 1:
                            Image("lateral_image")
                                .resizable()
                                .frame(width: 44, height: 44)
                                .padding(22)
                        case 2:
                            Text("🐽")
                                .frame(width: 88, height: 88)
                                .font(.system(size: 44))
                                .foregroundStyle(.white)
                        default:
                            Text("🎲")
                                .frame(width: 88, height: 88)
                                .font(.system(size: 44))
                                .foregroundStyle(.white)
                        }
                    }
                    .glassEffectCircle()
                    Spacer()
                    Button {
                        withAnimation {
                            microphone.toggle()
                        }
                        feedback.impactOccurred()
                        if microphone {
                            AudioAcquisitionUtil.shared.start()
                        } else {
                            AudioAcquisitionUtil.shared.stop()
                        }
                    } label: {
                        if microphone {
                            Image(systemName: "microphone")
                                .frame(width: 88, height: 88)
                                .font(.system(size: 44))
                                .foregroundStyle(.white)
                                .symbolVariant(volume > 0.3 ? .fill : .none)
                        } else {
                            Image(systemName: "microphone.slash")
                                .frame(width: 88, height: 88)
                                .font(.system(size: 44))
                                .foregroundStyle(.white)
                        }
                    }
                    .glassEffectCircle()
                    Button {
                        withAnimation {
                            showPhoneScreen.toggle()
                        }
                        feedback.impactOccurred()
                        if showPhoneScreen {
                            appState.sendWebSocketMessage(.onPhoneScreen, deviceMac.toData())
                        } else {
                            appState.sendWebSocketMessage(.offPhoneScreen, deviceMac.toData())
                        }
                    } label: {
                        if showPhoneScreen {
                            Image(systemName: "iphone.gen1.badge.play")
                                .frame(width: 88, height: 88)
                                .font(.system(size: 44))
                                .foregroundStyle(.accent)
                        } else {
                            Image(systemName: "iphone.gen1.badge.play")
                                .frame(width: 88, height: 88)
                                .font(.system(size: 44))
                                .foregroundStyle(.white)
                        }
                    }
                    .glassEffectCircle()
                }
            }
            .padding()
        }
        .onAppear {
            AudioAcquisitionUtil.shared.onAudioData = { data in
                
            }
            AudioAcquisitionUtil.shared.onDecibel = { value in
                self.volume = CGFloat(value)
            }
            
            /// Register audio and video listener callbacks
            WebSocketUtil.shared.addObserver(for: tag) { (message: URLSessionWebSocketTask.Message) in
                switch message {
                case .data(let data):
                    let result = appState.parseMessage(message: data)
                    if let msgType = result.0, let parsedData = result.1 {
                        switch msgType {
                        case MsgType.jpeg:
                            DispatchQueue.main.async {
                                cameraImage = parsedData
                            }
                        case MsgType.hangupCall:
                            // Hang up the call
                            
                            print("StackChan hung up the call")
                            
                            DispatchQueue.main.async {
                                dismiss()
                            }
                        default:
                            break
                        }
                    }
                case .string(let text):
                    print("Received text message: \(text)")
                @unknown default:
                    break
                }
            }
            // Turn on device camera
            
            appState.sendWebSocketMessage(.onCamera,deviceMac.toData())
        }
        .onDisappear {
            WebSocketUtil.shared.removeObserver(for: tag)
            appState.sendWebSocketMessage(.offPhoneScreen, deviceMac.toData())
            // Turn off device camera
            appState.sendWebSocketMessage(.offCamera,deviceMac.toData())
            if deviceMac != appState.deviceMac {
                appState.sendWebSocketMessage(.hangupCall)
            }
        }
        .toolbar(.hidden, for: .tabBar)
        .preferredColorScheme(.dark)
        .navigationTitle("AVATAR")
        .navigationBarTitleDisplayMode(.inline)
    }
    // Compress phone screen image and send to StackChan
    private func compressMobilePhoneScreen(image: UIImage) {
        if let jpegData = image.compress(to: stackChanTargetSize, memorySize: 0.02, cropCenter: true) {
            guard let macData = deviceMac.toData() else { return }
            let data = macData + jpegData
            appState.sendWebSocketMessage(.jpeg, data)
        }
    }
    
    // Detect head motion data from AR session
    private func detectHeadData(session: ARSession,faceAnchor: ARFaceAnchor) -> MotionData {
        // Get face transform in world coordinate space
        let faceTransform = faceAnchor.transform
        
        // Get camera transform of the current frame (phone position and orientation in world space)
        guard let cameraTransform = session.currentFrame?.camera.transform else {
            return MotionData(pitchServo: MotionDataItem(angle: 0, speed: 500),
                              yawServo: MotionDataItem(angle: 0, speed: 500))
        }
        
        // Relative transform = inverse camera transform × face transform
        let relativeTransform = simd_mul(simd_inverse(cameraTransform), faceTransform)
        let relativeMatrix = SCNMatrix4(relativeTransform)
        
        // Extract yaw and pitch angles from relative rotation matrix
        let pitch = atan2(relativeMatrix.m31, relativeMatrix.m33)        // Vertical rotation (pitch)
        let yaw = asin(-relativeMatrix.m32)                              // Horizontal rotation (yaw)
        
        // Convert radians to degrees
        let pitchDeg = pitch * 180.0 / .pi
        let yawDeg = yaw * 180.0 / .pi
        
        // Map yaw angle to servo range (-1280 to 1280)
        let yawServoAngle = max(-1280, min(1280, Int(-yawDeg * 20)))
        
        // Map pitch angle to servo range (0 to 900)
        // Looking straight = 0, looking up = 900
        let pitchServoAngle = max(0, min(900, Int(-pitchDeg * 10)))
        
        let pitchItem = MotionDataItem(angle: pitchServoAngle, speed: 500)
        let yawItem = MotionDataItem(angle: yawServoAngle, speed: 500)
        
        return MotionData(pitchServo: pitchItem, yawServo: yawItem)
    }
    
    // Build ExpressionData from blendShapes
    private func buildExpressionData(faceAnchor: ARFaceAnchor) -> ExpressionData {
        let blendShapes = faceAnchor.blendShapes
        
        // Left eye blink amount mapped to 0~100
        let eyeBlinkLeft = blendShapes[.eyeBlinkLeft]?.floatValue ?? 0
        let leftEyeWeight = max(0, min(100, Int((1.0 - eyeBlinkLeft) * 100)))
        
        // Right eye blink amount mapped to 0~100
        let eyeBlinkRight = blendShapes[.eyeBlinkRight]?.floatValue ?? 0
        let rightEyeWeight = max(0, min(100, Int((1.0 - eyeBlinkRight) * 100)))
        
        // Build ExpressionItem
        let leftEye = ExpressionItem(
            x: max(-100, min(100, Int(faceAnchor.lookAtPoint.x * 800))),
            y: max(-100, min(100, Int(-faceAnchor.lookAtPoint.y * 500))),
            rotation: 0,
            weight: leftEyeWeight
        )
        
        let rightEye = ExpressionItem(
            x: max(-100, min(100, Int(faceAnchor.lookAtPoint.x * 800))),
            y: max(-100, min(100, Int(-faceAnchor.lookAtPoint.y * 500))),
            rotation: 0,
            weight: rightEyeWeight
        )
        
        // Mouth
        let jawOpen = blendShapes[.jawOpen]?.floatValue ?? 0
        let mouthSmileLeft = blendShapes[.mouthSmileLeft]?.floatValue ?? 0
        let mouthSmileRight = blendShapes[.mouthSmileRight]?.floatValue ?? 0
        
        // Calculate X and Y offsets
        let mouthX = max(-100, min(100, Int((mouthSmileRight - mouthSmileLeft) * 100)))
        
        // Calculate mouth open weight
        let mouthWeight = max(0, min(100, Int(jawOpen * 100)))
        
        let mouth = ExpressionItem(
            x: mouthX,
            y: 0,
            rotation: 0,
            weight: mouthWeight
        )
        
        var expressionData = ExpressionData(leftEye: leftEye,
                                            rightEye: rightEye,
                                            mouth: mouth)
        
        //        // Start emotion-based adjustment
        if isHappy(blendShapes: blendShapes) {
            // Happy
            expressionData.leftEye.weight -= 35
            expressionData.leftEye.rotation = -2150
            expressionData.rightEye.weight -= 35
            expressionData.rightEye.rotation = 2150
        }
        //        if isShy(faceAnchor: faceAnchor, blendShapes: blendShapes) {
        //            // Shy
        //        }
        //        if isAmazed(blendShapes: blendShapes) {
        //            // Amazed
        //        }
        if isAnger(blendShapes: blendShapes) {
            // Angry
            expressionData.leftEye.rotation = 450
            expressionData.rightEye.rotation = -450
        }
        //        if isTired(blendShapes: blendShapes) {
        //            // Tired
        //        }
        return expressionData
    }
    
    /// Main emotion detection function
    private func emotionDetection(session: ARSession,anchors: [ARAnchor]) {
        var detectedEmotions: [String] = []
        if let anchor = anchors.first {
            guard let faceAnchor = anchor as? ARFaceAnchor else { return }
            let faceData = buildExpressionData(faceAnchor: faceAnchor)
            let headData = detectHeadData(session:session,faceAnchor: faceAnchor)
            withAnimation {
                self.expressionData = faceData
                self.headData = headData
            }
            
            /// Send data via Bluetooth
            let now = Date()
            if now.timeIntervalSince(lastSendTime) >= 0.5 {
                self.sendExpressionData(data: faceData)
                self.sendHeadData(data: headData)
                lastSendTime = now
            }
            
            
            let blendShapes = faceAnchor.blendShapes
            
            // Detect five basic emotions
            if isHappy(blendShapes: blendShapes) {
                detectedEmotions.append("Happy")
            }
            if isShy(faceAnchor: faceAnchor, blendShapes: blendShapes) {
                detectedEmotions.append("Shy")
            }
            if isAmazed(blendShapes: blendShapes) {
                detectedEmotions.append("Amazed")
            }
            if isAnger(blendShapes: blendShapes) {
                detectedEmotions.append("Angry")
            }
            if isTired(blendShapes: blendShapes) {
                detectedEmotions.append("Tired")
            }
            
            // Add gaze and head direction detection
            detectedEmotions.append(getGazeDirection(faceAnchor: faceAnchor))
            detectedEmotions.append(getHeadDirection(faceAnchor: faceAnchor))
        }
        withAnimation {
            self.emotions = detectedEmotions
        }
    }
    
    private func sendExpressionData(data : ExpressionData) {
        let jsonString = deviceMac + data.toJsonString()
        let data = jsonString.toData()
        appState.sendWebSocketMessage(.controlAvatar, data)
    }
    
    private func sendHeadData(data : MotionData) {
        let jsonString = deviceMac + data.toJsonString()
        let data = jsonString.toData()
        appState.sendWebSocketMessage(.controlMotion, data)
    }
    
    /// Happy emotion detection
    private func isHappy(blendShapes: [ARFaceAnchor.BlendShapeLocation: NSNumber]) -> Bool {
        let smileLeft = blendShapes[.mouthSmileLeft]?.floatValue ?? 0
        let smileRight = blendShapes[.mouthSmileRight]?.floatValue ?? 0
        let eyeSquintLeft = blendShapes[.eyeSquintLeft]?.floatValue ?? 0
        let eyeSquintRight = blendShapes[.eyeSquintRight]?.floatValue ?? 0
        let cheekSquintLeft = blendShapes[.cheekSquintLeft]?.floatValue ?? 0
        let cheekSquintRight = blendShapes[.cheekSquintRight]?.floatValue ?? 0
        
        // Calculate overall smile intensity
        let smileIntensity = (smileLeft + smileRight) / 2
        let eyeSquintIntensity = (eyeSquintLeft + eyeSquintRight) / 2
        let cheekSquintIntensity = (cheekSquintLeft + cheekSquintRight) / 2
        
        // Happy expression requires a clear smile with eye muscle involvement
        return smileIntensity > emotionThresholds.happy.smile &&
        (eyeSquintIntensity > emotionThresholds.happy.eyeSquint ||
         cheekSquintIntensity > emotionThresholds.happy.cheekSquint)
    }
    
    /// Shy emotion detection
    private func isShy(faceAnchor: ARFaceAnchor, blendShapes: [ARFaceAnchor.BlendShapeLocation: NSNumber]) -> Bool {
        // 1. Slight or clear head tilt downward
        let transform = faceAnchor.transform
        let rotation = SCNMatrix4(transform)
        let pitch = asin(-rotation.m32) // 上下旋转
        let isHeadDown = pitch > emotionThresholds.shy.headPitch
        
        // 2. Mouth closed with a slight smile
        let mouthClose = blendShapes[.mouthClose]?.floatValue ?? 0
        let smileLeft = blendShapes[.mouthSmileLeft]?.floatValue ?? 0
        let smileRight = blendShapes[.mouthSmileRight]?.floatValue ?? 0
        let smileIntensity = (smileLeft + smileRight) / 2
        let isMouthClosedSmile = mouthClose > emotionThresholds.shy.mouthPress && smileIntensity > emotionThresholds.shy.smile
        
        // 3. Eyes looking sideways or downward
        let lookAt = faceAnchor.lookAtPoint
        let isLookingSideways = abs(lookAt.x) > emotionThresholds.gaze.xThreshold // Looking left or right
        let isLookingDown = lookAt.y < -emotionThresholds.gaze.yThreshold // Looking downward
        
        return isHeadDown && isMouthClosedSmile && (isLookingSideways || isLookingDown)
    }
    
    /// Amazed emotion detection
    private func isAmazed(blendShapes: [ARFaceAnchor.BlendShapeLocation: NSNumber]) -> Bool {
        let jawOpen = blendShapes[.jawOpen]?.floatValue ?? 0
        let eyeWideLeft = blendShapes[.eyeWideLeft]?.floatValue ?? 0
        let eyeWideRight = blendShapes[.eyeWideRight]?.floatValue ?? 0
        let browInnerUp = blendShapes[.browInnerUp]?.floatValue ?? 0
        let mouthFunnel = blendShapes[.mouthFunnel]?.floatValue ?? 0
        
        // Amazed traits: wide eyes + raised brows + (open mouth or O-shape)
        let isEyesWide = (eyeWideLeft + eyeWideRight) / 2 > emotionThresholds.amazed.eyeWide
        let isBrowRaised = browInnerUp > emotionThresholds.amazed.browInnerUp
        let isMouthAction = jawOpen > emotionThresholds.amazed.jawOpen ||
        mouthFunnel > emotionThresholds.amazed.mouthFunnel
        
        return isEyesWide && isBrowRaised && isMouthAction
    }
    
    /// Angry emotion detection
    private func isAnger(blendShapes: [ARFaceAnchor.BlendShapeLocation: NSNumber]) -> Bool {
        // Brow features
        let browDownLeft = blendShapes[.browDownLeft]?.floatValue ?? 0
        let browDownRight = blendShapes[.browDownRight]?.floatValue ?? 0
        
        // Eye features
        let eyeSquintLeft = blendShapes[.eyeSquintLeft]?.floatValue ?? 0
        let eyeSquintRight = blendShapes[.eyeSquintRight]?.floatValue ?? 0
        
        // Mouth features
        let mouthFrownLeft = blendShapes[.mouthFrownLeft]?.floatValue ?? 0
        let mouthFrownRight = blendShapes[.mouthFrownRight]?.floatValue ?? 0
        let mouthPressLeft = blendShapes[.mouthPressLeft]?.floatValue ?? 0
        let mouthPressRight = blendShapes[.mouthPressRight]?.floatValue ?? 0
        
        // Nose features
        let noseSneerLeft = blendShapes[.noseSneerLeft]?.floatValue ?? 0
        let noseSneerRight = blendShapes[.noseSneerRight]?.floatValue ?? 0
        
        // Calculate averages
        let avgBrowDown = (browDownLeft + browDownRight) / 2
        let avgEyeSquint = (eyeSquintLeft + eyeSquintRight) / 2
        let avgMouthFrown = (mouthFrownLeft + mouthFrownRight) / 2
        let avgMouthPress = (mouthPressLeft + mouthPressRight) / 2
        let avgNoseSneer = (noseSneerLeft + noseSneerRight) / 2
        
        // Anger scoring system
        var angerScore = 0
        
        if avgBrowDown > emotionThresholds.anger.browDown { angerScore += 3 }
        if avgEyeSquint > emotionThresholds.anger.eyeSquint { angerScore += 2 }
        if avgMouthFrown > emotionThresholds.anger.mouthFrown { angerScore += 2 }
        if avgMouthPress > emotionThresholds.anger.mouthPress { angerScore += 1 }
        if avgNoseSneer > emotionThresholds.anger.noseSneer { angerScore += 1 }
        
        // Must reach threshold and include brow-down feature
        return angerScore >= emotionThresholds.anger.minScore &&
        avgBrowDown > emotionThresholds.anger.browDown
    }
    
    /// Tired emotion detection
    private func isTired(blendShapes: [ARFaceAnchor.BlendShapeLocation: NSNumber]) -> Bool {
        let eyeBlinkLeft = blendShapes[.eyeBlinkLeft]?.floatValue ?? 0
        let eyeBlinkRight = blendShapes[.eyeBlinkRight]?.floatValue ?? 0
        let eyeSquintLeft = blendShapes[.eyeSquintLeft]?.floatValue ?? 0
        let eyeSquintRight = blendShapes[.eyeSquintRight]?.floatValue ?? 0
        
        // Tired traits: eyes closed or squinting
        let eyesClosed = (eyeBlinkLeft > emotionThresholds.tired.eyeClose &&
                          eyeBlinkRight > emotionThresholds.tired.eyeClose) ||
        (eyeSquintLeft > emotionThresholds.tired.eyeSquint &&
         eyeSquintRight > emotionThresholds.tired.eyeSquint)
        
        return eyesClosed
    }
    
    // MARK: - Helper Functions
    
    /// Get gaze direction
    private func getGazeDirection(faceAnchor: ARFaceAnchor) -> String {
        let lookAtPoint = faceAnchor.lookAtPoint
        var direction = ""
        
        if lookAtPoint.x < -emotionThresholds.gaze.xThreshold {
            direction += "Left"
        } else if lookAtPoint.x > emotionThresholds.gaze.xThreshold {
            direction += "Right"
        }
        
        if lookAtPoint.y < -emotionThresholds.gaze.yThreshold {
            direction += "Down"
        } else if lookAtPoint.y > emotionThresholds.gaze.yThreshold {
            direction += "Up"
        }
        
        return direction.isEmpty ? "Looking Forward" : direction + " Look"
    }
    
    /// Get head direction
    private func getHeadDirection(faceAnchor: ARFaceAnchor) -> String {
        let transform = faceAnchor.transform
        let rotation = SCNMatrix4(transform)
        let yaw = atan2(rotation.m31, rotation.m33)
        let pitch = asin(-rotation.m32)
        
        var horizontal = ""
        var vertical = ""
        
        if yaw < -emotionThresholds.head.yawThreshold {
            horizontal = "Left"
        } else if yaw > emotionThresholds.head.yawThreshold {
            horizontal = "Right"
        }
        
        // Correct vertical direction
        if pitch < -emotionThresholds.head.pitchThreshold {
            vertical = "Up"
        } else if pitch > emotionThresholds.head.pitchThreshold {
            vertical = "Down"
        }
        
        if horizontal.isEmpty && vertical.isEmpty {
            return "Head Facing Forward"
        } else {
            return "Head Facing " + vertical + horizontal
        }
    }
}

// MARK: - Threshold Configuration

private struct EmotionThresholds {
    // Happy emotion thresholds
    struct Happy {
        let smile: Float = 0.3
        let eyeSquint: Float = 0.15
        let cheekSquint: Float = 0.1
    }
    
    // Shy emotion thresholds
    struct Shy {
        let headPitch: Float = 0.08
        let eyeSquint: Float = 0.1
        let mouthPress: Float = 0.25
        let smile: Float = 0.15
    }
    
    // Amazed emotion thresholds
    struct Amazed {
        let eyeWide: Float = 0.4
        let browInnerUp: Float = 0.3
        let jawOpen: Float = 0.4
        let mouthFunnel: Float = 0.3
    }
    
    // Angry emotion thresholds
    struct Anger {
        let browDown: Float = 0.35
        let eyeSquint: Float = 0.25
        let mouthFrown: Float = 0.2
        let mouthPress: Float = 0.2
        let noseSneer: Float = 0.15
        let minScore: Int = 5
    }
    
    // Tired emotion thresholds
    struct Tired {
        let eyeClose: Float = 0.7
        let eyeSquint: Float = 0.5
        let jawOpen: Float = 0.3
    }
    
    // Gaze detection thresholds
    struct Gaze {
        let xThreshold: Float = 0.02
        let yThreshold: Float = 0.02
    }
    
    // Head direction thresholds
    struct Head {
        let yawThreshold: Float = 0.25
        let pitchThreshold: Float = 0.25
    }
    
    let happy = Happy()
    let shy = Shy()
    let amazed = Amazed()
    let anger = Anger()
    let tired = Tired()
    let gaze = Gaze()
    let head = Head()
}

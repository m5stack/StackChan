/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

//
//  StackChanArView.swift
//  Runner
//
// Created by on 2026/2/5.
//

import RealityKit
import ARKit

class StackChanArView : NSObject, FlutterPlatformView, ARSessionDelegate, ARSCNViewDelegate {
    
    private var arView: ARSCNView
    private var channel: FlutterMethodChannel?
    private var expressionChannel: FlutterEventChannel?
    private var frameChannel: FlutterEventChannel?
    private var expressionStreamHandler: ExpressionStreamHandler?
    private var frameStreamHandler: FrameStreamHandler?
    private var decorate: Int = 0
    private var captureScreen: Bool = false
    private let methodChannelName = "com.stackchan.ar.view"
    private var lastCaptureTime: TimeInterval = 0
    private let stackChanTargetSize = CGSize(width: 320, height: 240)
    private var faceAnchorNode: SCNNode?
    private var currentDecorationNode: SCNNode?
    private var expressionLayer = ExpressionLayer(data: ExpressionData(leftEye: ExpressionItem(), rightEye: ExpressionItem(), mouth: ExpressionItem()),reverse: true)
    private let emotionThresholds = EmotionThresholds()
    private var lastSendTime: Date = Date(timeIntervalSince1970: 0)
    
    func view() -> UIView {
        return arView
    }
    
    init(
        frame: CGRect,
        viewId: Int64,
        messenger: FlutterBinaryMessenger,
        args: Any?
    ) {
        arView = ARSCNView(frame: frame)
        arView.contentMode = .scaleAspectFit
        arView.autoresizingMask = [.flexibleWidth,.flexibleHeight]
        super.init()
        initializeChannels(viewId: viewId, messenger: messenger)
        setupARSession()
    }
    
    private func initializeChannels(viewId: Int64, messenger: FlutterBinaryMessenger) {
        let methodChannelName = "\(methodChannelName)_\(viewId)"
        channel = FlutterMethodChannel(name: methodChannelName, binaryMessenger: messenger)
        channel?.setMethodCallHandler { [weak self] call, result in
            self?.handleMethodCall(call, result: result)
        }
        
        // translated comment
        let expressionChannelName = "\(methodChannelName)_expression"
        expressionStreamHandler = ExpressionStreamHandler()
        expressionChannel = FlutterEventChannel(name: expressionChannelName, binaryMessenger: messenger)
        expressionChannel?.setStreamHandler(expressionStreamHandler)
        
        // translated comment
        let frameChannelName = "\(methodChannelName)_frame"
        frameStreamHandler = FrameStreamHandler()
        frameChannel = FlutterEventChannel(name: frameChannelName, binaryMessenger: messenger)
        frameChannel?.setStreamHandler(frameStreamHandler)
    }
    
    private func setupARSession() {
        guard ARFaceTrackingConfiguration.isSupported else {
                        return
        }
        let configuration = ARFaceTrackingConfiguration()
        configuration.isLightEstimationEnabled = true
        if let format = ARFaceTrackingConfiguration.supportedVideoFormats.last {
            configuration.videoFormat = format
        }
        configuration.videoHDRAllowed = true
        arView.automaticallyUpdatesLighting = true
        arView.session.delegate = self
        arView.delegate = self
        arView.session.run(configuration, options: [.resetTracking,.removeExistingAnchors])
    }
    
    private func handleMethodCall(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        switch call.method {
        case "dispose":
            dispose()
            result(nil)
        case "setDecorate":
            if let decorateValue = call.arguments as? Int {
                decorate = decorateValue
                if let faceNode = self.faceAnchorNode {
                    self.updateDecorationOnNode(node: faceNode, decorate: decorate)
                }
            }
            result(nil)
        case "setCaptureScreen":
            if let captureValue = call.arguments as? Bool {
                captureScreen = captureValue
            }
            result(nil)
        default:
            result(FlutterMethodNotImplemented)
        }
    }
    
    //translated comment
    func session(_ session: ARSession, didUpdate anchors: [ARAnchor]) {
        DispatchQueue.main.async {
            self.emotionDetection(session: session, anchors: anchors)
        }
    }
    
    private func emotionDetection(session: ARSession, anchors: [ARAnchor]) {
        if let anchor = anchors.first {
            guard let faceAnchor = anchor as? ARFaceAnchor else { return }
            let faceData = buildExpressionData(faceAnchor: faceAnchor)
            let headData = detectHeadData(session:session,faceAnchor: faceAnchor)
            self.updateDecoration(expressionData: faceData)
            
            let now = Date()
            if now.timeIntervalSince(self.lastSendTime) >= 0.5 {
                let danceData = DanceData(leftEye: faceData.leftEye, rightEye: faceData.rightEye, mouth: faceData.mouth, yawServo: headData.yawServo, pitchServo: headData.pitchServo, durationMs: 1000)
                let jsonString = danceData.toJsonString()
                self.expressionStreamHandler?.sendExpressionData(jsonString)
                lastSendTime = now
            }
        }
    }
    
    private func detectHeadData(session: ARSession,faceAnchor: ARFaceAnchor) -> MotionData {
        let faceTransform = faceAnchor.transform
        guard let cameraTransform = session.currentFrame?.camera.transform else {
            return MotionData(pitchServo: MotionDataItem(angle: 0, speed: 500),
                              yawServo: MotionDataItem(angle: 0, speed: 500))
        }
        let relativeTransform = simd_mul(simd_inverse(cameraTransform), faceTransform)
        let relativeMatrix = SCNMatrix4(relativeTransform)
        let pitch = atan2(relativeMatrix.m31, relativeMatrix.m33)        // Vertical rotation (pitch)
        let yaw = asin(-relativeMatrix.m32)                              // Horizontal rotation (yaw)
        let pitchDeg = pitch * 180.0 / .pi
        let yawDeg = yaw * 180.0 / .pi
        let yawServoAngle = max(-1280, min(1280, Int(-yawDeg * 20)))
        let pitchServoAngle = max(0, min(900, Int(-pitchDeg * 10)))
        let pitchItem = MotionDataItem(angle: pitchServoAngle, speed: 500)
        let yawItem = MotionDataItem(angle: yawServoAngle, speed: 500)
        return MotionData(pitchServo: pitchItem, yawServo: yawItem)
    }
    
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
    
    private func buildExpressionData(faceAnchor: ARFaceAnchor) -> ExpressionData {
        let blendShapes = faceAnchor.blendShapes
        let eyeBlinkLeft = blendShapes[.eyeBlinkLeft]?.floatValue ?? 0
        let leftEyeWeight = max(0, min(100, Int((1.0 - eyeBlinkLeft) * 100)))
        let eyeBlinkRight = blendShapes[.eyeBlinkRight]?.floatValue ?? 0
        let rightEyeWeight = max(0, min(100, Int((1.0 - eyeBlinkRight) * 100)))
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
        let jawOpen = blendShapes[.jawOpen]?.floatValue ?? 0
        let mouthSmileLeft = blendShapes[.mouthSmileLeft]?.floatValue ?? 0
        let mouthSmileRight = blendShapes[.mouthSmileRight]?.floatValue ?? 0
        let mouthX = max(-100, min(100, Int((mouthSmileRight - mouthSmileLeft) * 100)))
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
        if isHappy(blendShapes: blendShapes) {
            expressionData.leftEye.weight -= 35
            expressionData.leftEye.rotation = -2150
            expressionData.rightEye.weight -= 35
            expressionData.rightEye.rotation = 2150
        }
        if isAnger(blendShapes: blendShapes) {
            expressionData.leftEye.rotation = 450
            expressionData.rightEye.rotation = -450
        }
        return expressionData
    }
    
    
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
    
    private func isShy(faceAnchor: ARFaceAnchor, blendShapes: [ARFaceAnchor.BlendShapeLocation: NSNumber]) -> Bool {
        // 1. Slight or clear head tilt downward
        let transform = faceAnchor.transform
        let rotation = SCNMatrix4(transform)
        let pitch = asin(-rotation.m32) // translated comment
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
    
    func renderer(_ renderer: any SCNSceneRenderer, nodeFor anchor: ARAnchor) -> SCNNode? {
        guard anchor is ARFaceAnchor else { return nil }
        let node = SCNNode()
        self.faceAnchorNode = node
        updateDecorationOnNode(node: node, decorate: decorate)
        return node
    }
    
    private func updateDecorationOnNode(node: SCNNode, decorate: Int) {
        currentDecorationNode?.removeFromParentNode()
        
        if decorate == 1 {
            let container = SCNNode()
            
            let stackChanModelNode = createStackChanModel()
            container.addChildNode(stackChanModelNode)
            
            let expressionPlaneNode = createPlane()
            container.addChildNode(expressionPlaneNode)
            
            node.addChildNode(container)
            currentDecorationNode = container
        } else if decorate == 2 {
            let noseNode = createEmojiNoseNode(emoji: "🐽")
            node.addChildNode(noseNode)
            currentDecorationNode = noseNode
        }
    }
    
    private func createEmojiNoseNode(emoji: String) -> SCNNode {
        let size = CGSize(width: 300, height: 300)
        UIGraphicsBeginImageContextWithOptions(size, false, 0)
        (emoji as NSString).draw(in: CGRect(origin: .zero, size: size),
                                 withAttributes: [.font: UIFont.systemFont(ofSize: size.width - 20)])
        let image = UIGraphicsGetImageFromCurrentImageContext()
        UIGraphicsEndImageContext()
        
        let nosePlane = SCNPlane(width: 0.05, height: 0.05)
        nosePlane.firstMaterial?.diffuse.contents = image
        nosePlane.firstMaterial?.isDoubleSided = true
        
        let noseNode = SCNNode(geometry: nosePlane)
        noseNode.name = "noseNode"
        noseNode.position = SCNVector3(0, 0, 0.07)
        return noseNode
    }
    
    private func createPlane() -> SCNNode {
        let plane = SCNPlane(width: 0.16, height: 0.12)
        
        let layerWidth = plane.width * 1000
        let layerHeight = plane.height * 1000
        expressionLayer.frame = CGRect(origin: .zero, size: CGSize(width: layerWidth, height: layerHeight))
        expressionLayer.setNeedsDisplay()
        
        let material = SCNMaterial()
        material.diffuse.contents = UIColor.black
        plane.materials = [material]
        
        let planeNode = SCNNode(geometry: plane)
        planeNode.name = "expressionPlane"
        planeNode.position = SCNVector3(0, 0.03, 0.07)
        return planeNode
    }
    
    private func createStackChanModel() -> SCNNode {
        guard let scene = SCNScene(named: "StackChanModel.scn"),
              let modelNode = scene.rootNode.childNodes.first else {
                        return SCNNode()
        }
        modelNode.name = "StackChanModel"
        modelNode.scale = SCNVector3(0.004, 0.004, 0.004)
        modelNode.opacity = 0.4
        modelNode.position = SCNVector3(0, 0.03, 0)
        modelNode.eulerAngles = SCNVector3Zero
        modelNode.eulerAngles.x = -Float.pi / 2
        
        //translated comment
        if let foundation = modelNode.childNode(withName: "_00_stackchan450_3",recursively: false),let centralComponent = modelNode.childNode(withName: "_00_stackchan450_2", recursively: false) {
            foundation.opacity = 0
            centralComponent.opacity = 0
        }
        
        return modelNode
    }
    
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
    
    private lazy var expressionRenderer: UIGraphicsImageRenderer = {
        let format = UIGraphicsImageRendererFormat.default()
        format.scale = UIScreen.main.scale
        format.opaque = false
        return UIGraphicsImageRenderer(
            size: expressionLayer.bounds.size,
            format: format
        )
    }()
    
    //translated comment
    func renderer(_ renderer: any SCNSceneRenderer, updateAtTime time: TimeInterval) {
        if captureScreen {
            if time - lastCaptureTime >= 0.5 {
                lastCaptureTime = time
                DispatchQueue.main.async { [weak self] in
                    guard let self = self else { return }
                    let renderedImage = arView.snapshot()
                    if let jpedData = renderedImage.compress(to: stackChanTargetSize,memorySize: 0.02,cropCenter: true) {
                        self.frameStreamHandler?.sendFrameData(jpedData)
                    }
                }
            }
        }
    }
    
    //translated comment
    private func updateDecoration(expressionData: ExpressionData) {
        DispatchQueue.main.async {
            if self.decorate == 1 {
                let scene = self.arView.scene
                guard let planeNode = scene.rootNode.childNode(withName: "expressionPlane", recursively: true),
                      let plane = planeNode.geometry as? SCNPlane else {
                    return
                }
                self.expressionLayer.data = expressionData
                self.expressionLayer.setNeedsDisplay()
                let originalImage = self.expressionRenderer.image { ctx in
                    self.expressionLayer.render(in: ctx.cgContext)
                }
                let image = UIImage(
                    cgImage: originalImage.cgImage!,
                    scale: originalImage.scale,
                    orientation: .upMirrored
                )
                plane.materials.first?.diffuse.contents = image
            }
        }
    }
    
    private func dispose() {
        arView.session.pause()
        channel?.setMethodCallHandler(nil)
        expressionChannel?.setStreamHandler(nil)
        frameChannel?.setStreamHandler(nil)
        arView.scene.rootNode.childNodes.forEach { $0.removeFromParentNode() }
    }
}



class ExpressionStreamHandler: NSObject,FlutterStreamHandler {
    
    private var eventSink: FlutterEventSink?
    
    func sendExpressionData(_ data: String) {
        guard let sink = eventSink else { return }
        DispatchQueue.main.async {
            sink(data)
        }
    }
    
    func onListen(withArguments arguments: Any?, eventSink events: @escaping FlutterEventSink) -> FlutterError? {
        eventSink = events
        return nil
    }
    
    func onCancel(withArguments arguments: Any?) -> FlutterError? {
        eventSink = nil
        return nil
    }
}

class FrameStreamHandler: NSObject, FlutterStreamHandler {
    
    private var eventSink: FlutterEventSink?
    
    func sendFrameData(_ data: Data) {
        guard let sink = eventSink else { return }
        
        // translated comment
        DispatchQueue.main.async {
            sink(FlutterStandardTypedData(bytes: data))
        }
    }
    
    func onListen(withArguments arguments: Any?, eventSink events: @escaping FlutterEventSink) -> FlutterError? {
        eventSink = events
        return nil
    }
    
    func onCancel(withArguments arguments: Any?) -> FlutterError? {
        eventSink = nil
        return nil
    }
}


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

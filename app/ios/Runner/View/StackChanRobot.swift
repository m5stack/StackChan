//
//  StackChanRobot.swift
//  Runner
//
//  Created by 袁智鸿 on 2026/1/30.
//
import SceneKit

class StackChanRobot: NSObject, FlutterPlatformView, FlutterStreamHandler {
    
    private let sceneView: SCNView
    private let methodChannel: FlutterMethodChannel
    
    private var currentDanceData: DanceData?
    private let expressionLayer = ExpressionLayer(data: ExpressionData(leftEye: ExpressionItem(weight: 100), rightEye: ExpressionItem(weight: 100), mouth: ExpressionItem()))
    private let planeNodeName = "expressionPlane"
    private let rotateKey = "autoRotate"
    private var topLook: Bool = false
    private let methodChannelName = "com.stackchan.robot.method"
    
    private var defaultCameraNode: SCNNode?
    private var topCameraNode: SCNNode?
    
    init(
        frame: CGRect,
        viewId: Int64,
        messenger: FlutterBinaryMessenger,
        args: Any?
    ) {
        self.sceneView = SCNView(frame: frame)
        self.methodChannel = FlutterMethodChannel(
            name: methodChannelName + "_\(viewId)",
            binaryMessenger: messenger
        )
        super.init()
        methodChannel.setMethodCallHandler(handleMethodCall)
        setupSceneView()
        setupInitialScene()
    }
    
    func view() -> UIView {
        return sceneView
    }
    
    func onListen(withArguments arguments: Any?, eventSink events: @escaping FlutterEventSink) -> FlutterError? {
        return nil
    }
    
    func onCancel(withArguments arguments: Any?) -> FlutterError? {
        return nil
    }
    
    private func handleMethodCall(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        switch call.method {
        case "updateDanceData":
            if let json = call.arguments as? String {
                updateDanceData(from: json)
                result(nil)
            } else {
                result(FlutterError(
                    code: "INVALID_ARGS",
                    message: "Expected JSON string",
                    details: nil
                ))
            }
        case "setTopLook":
            if let topLook = call.arguments as? Bool {
                self.topLook = topLook
                setupCamera()
                result(nil)
            } else {
                result(FlutterError(
                    code: "INVALID_ARGS",
                    message: "Expected boolean value",
                    details: nil
                ))
            }
        case "setAllowsCameraControl":
            if let allowsControl = call.arguments as? Bool {
                sceneView.allowsCameraControl = allowsControl
                result(nil)
            } else {
                result(FlutterError(
                    code: "INVALID_ARGS",
                    message: "Expected boolean value",
                    details: nil
                ))
            }
        case "dispose":
            cleanup()
            result(nil)
        default:
            result(FlutterMethodNotImplemented)
        }
    }
    
    private func setupSceneView() {
        sceneView.antialiasingMode = .multisampling4X
        sceneView.autoenablesDefaultLighting = true
        sceneView.allowsCameraControl = false
        sceneView.backgroundColor = .clear
        sceneView.isPlaying = true
    }
    
    private func setupInitialScene() {
        guard let scene = SCNScene(named: "StackChanModel.scn") else {
            print("Failed to load StackChanModel.scn")
            return
        }
        scene.rootNode.eulerAngles = SCNVector3Zero
        scene.rootNode.eulerAngles.x = -Float.pi / 2
        scene.rootNode.position.y = scene.rootNode.position.y + 25
        scene.rootNode.position.z = scene.rootNode.position.z - 35
        
        if let rootNode = scene.rootNode.childNodes.first {
            setupRobotHierarchy(rootNode: rootNode, scene: scene)
            
            if let defaultCamera = scene.rootNode.childNode(withName: "camera", recursively: true) {
                defaultCameraNode = defaultCamera
            } else {
                defaultCameraNode = createDefaultCameraNode(rootNode: rootNode)
                rootNode.addChildNode(defaultCameraNode!)
            }
            sceneView.pointOfView = defaultCameraNode
        }
        sceneView.scene = scene
    }
    
    private func createDefaultCameraNode(rootNode: SCNNode) -> SCNNode {
        let cameraNode = SCNNode()
        cameraNode.name = "defaultCamera"
        let camera = SCNCamera()
        camera.zFar = 200
        cameraNode.camera = camera
        cameraNode.position = SCNVector3(x: 0, y: -100, z: 0)
        let lookAtConstraint = SCNLookAtConstraint(target: rootNode)
        lookAtConstraint.isGimbalLockEnabled = true
        cameraNode.constraints = [lookAtConstraint]
        return cameraNode
    }
    
    private func createTopCameraNode(rootNode: SCNNode) -> SCNNode {
        let cameraNode = SCNNode()
        cameraNode.name = "leftTopCamera"
        let camera = SCNCamera()
        camera.zFar = 300
        cameraNode.camera = camera
        cameraNode.position = SCNVector3(x: 0, y: -100, z: 70)
        let lookAtConstraint = SCNLookAtConstraint(target: rootNode)
        lookAtConstraint.isGimbalLockEnabled = true
        cameraNode.constraints = [lookAtConstraint]
        return cameraNode
    }
    
    private func setupRobotHierarchy(rootNode: SCNNode, scene: SCNScene) {
        guard let foundation = rootNode.childNode(withName: "_00_stackchan450_3", recursively: false),
              let centralComponent = rootNode.childNode(withName: "_00_stackchan450_2", recursively: false),
              let head = rootNode.childNode(withName: "_00_stackchan450_1", recursively: false) else {
            return
        }
        
        let yawAxis = SCNNode()
        yawAxis.name = "yawAxis"
        let centralWorldPos = centralComponent.worldPosition
        yawAxis.worldPosition.z = centralWorldPos.z + 15
        foundation.addChildNode(yawAxis)
        
        let centralWorldTransform = centralComponent.worldTransform
        yawAxis.addChildNode(centralComponent)
        centralComponent.setWorldTransform(centralWorldTransform)
        
        // Setup pitch axis for head movement
        let headWorldTransform = head.worldTransform
        let pitchAxis = SCNNode()
        pitchAxis.name = "pitchAxis"
        pitchAxis.worldPosition.z = pitchAxis.worldPosition.z - 20
        centralComponent.addChildNode(pitchAxis)
        pitchAxis.addChildNode(head)
        head.setWorldTransform(headWorldTransform)
        
        // Add expression plane to head
        addExpressionPlane(to: head)
    }
    
    private func addExpressionPlane(to head: SCNNode) {
        let plane = SCNPlane(width: 42, height: 32)
        let magnification: CGFloat = 5
        let size = CGSize(width: magnification * plane.width, height: magnification * plane.height)
        
        expressionLayer.frame = CGRect(origin: .zero, size: size)
        expressionLayer.setNeedsDisplay()
        let material = SCNMaterial()
        plane.materials = [material]
        
        let planeNode = SCNNode(geometry: plane)
        planeNode.name = planeNodeName
        planeNode.position = head.position
        planeNode.position.z = planeNode.position.z - 4.5
        
        head.addChildNode(planeNode)
    }
    
    private func setupCamera() {
        guard let scene = sceneView.scene,
              let rootNode = scene.rootNode.childNodes.first else {
            return
        }
        rootNode.childNodes.filter { $0.name == "leftTopCamera" }.forEach { $0.removeFromParentNode() }
        if topLook {
            if topCameraNode == nil {
                topCameraNode = createTopCameraNode(rootNode: rootNode)
                rootNode.addChildNode(topCameraNode!)
            }
            sceneView.pointOfView = topCameraNode
        } else {
            if defaultCameraNode == nil {
                defaultCameraNode = createDefaultCameraNode(rootNode: rootNode)
                rootNode.addChildNode(defaultCameraNode!)
            }
            sceneView.pointOfView = defaultCameraNode
        }
    }
    
    private func updateDanceData(from json: String) {
        guard let danceData = DanceData.from(jsonString: json) else {
            return
        }
        
        currentDanceData = danceData
        
        DispatchQueue.main.async {
            self.applyDanceData(danceData)
        }
    }
    
    private func applyDanceData(_ data: DanceData) {
        guard let scene = sceneView.scene,
              let rootNode = scene.rootNode.childNodes.first else {
            return
        }
        
        // Update servo positions
        updateServos(rootNode: rootNode, data: data)
        
        // Update RGB color
        updateRGBColor(rootNode: rootNode, data: data)
        
        // Update expression
        updateExpression(data: data)
    }
    private func updateServos(rootNode: SCNNode, data: DanceData) {
        
        if let yawAxis = rootNode.childNode(withName: "yawAxis", recursively: true),
           let pitchAxis = rootNode.childNode(withName: "pitchAxis", recursively: true) {
            
            yawAxis.removeAction(forKey: rotateKey)
            
            // Update yaw (rotation around Y axis)
            if data.yawServo.rotate == 0 {
                let clampedYaw = max(-128, min(128, data.yawServo.angle / 10))
                let yawRadians = Float(clampedYaw) * Float.pi / 180.0
                yawAxis.rotation = SCNVector4(0, 1, 0, yawRadians)
            } else {
                let rotateSpeed = max(-100, min(100, data.yawServo.rotate / 10))
                let radiansPerSecond = Float(rotateSpeed) / 100.0 * Float.pi * 2
                
                let rotateAction = SCNAction.customAction(duration: .infinity) { node, _ in
                    let deltaTime: Float = 1.0 / 60.0
                    node.eulerAngles.y += radiansPerSecond * deltaTime
                }
                yawAxis.runAction(rotateAction, forKey: rotateKey)
            }
            
            // Update pitch (head tilt)
            let clampedPitch = max(0, min(90, data.pitchServo.angle / 10))
            let pitchRadians = Float(clampedPitch) * Float.pi / 180.0
            pitchAxis.eulerAngles.x = -pitchRadians
        }
    }
    
    private func updateRGBColor(rootNode: SCNNode, data: DanceData) {
        rootNode.enumerateChildNodes { node, _ in
            if let materials = node.geometry?.materials {
                for material in materials {
                    if material.name == "MTL12" {
                        if let color = UIColor(hex: data.leftRgbColor) {
                            material.emission.contents = color
                        }
                        break
                    }
                }
            }
        }
    }
    
    private func updateExpression(data: DanceData) {
        guard let planeNode = sceneView.scene?.rootNode.childNode(withName: planeNodeName, recursively: true),
              let plane = planeNode.geometry as? SCNPlane else {
            return
        }
        
        let expressionData = ExpressionData(
            leftEye: data.leftEye,
            rightEye: data.rightEye,
            mouth: data.mouth
        )
        
        expressionLayer.data = expressionData
        expressionLayer.setNeedsDisplay()
        
        let newImage = expressionRenderer().image { ctx in
            self.expressionLayer.render(in: ctx.cgContext)
        }
        
        plane.firstMaterial?.diffuse.contents = newImage
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
    
    private func cleanup() {
        // Stop all animations
        sceneView.scene?.rootNode.childNodes.forEach { node in
            node.removeAllActions()
            node.removeFromParentNode()
        }
        
        // Clean up scene
        sceneView.scene = nil
        sceneView.isPlaying = false
        
        // Remove method call handler
        methodChannel.setMethodCallHandler(nil)
    }
    
    deinit {
        cleanup()
    }
}





class ExpressionLayer: CALayer {
    var data: ExpressionData
    
    let reverse: Bool
    
    init(data: ExpressionData, reverse: Bool = false) {
        self.data = data
        self.reverse = reverse
        super.init()
        self.contentsScale = UIScreen.main.scale
        self.setNeedsDisplay()
    }
    
    override init(layer: Any) {
        if let layer = layer as? ExpressionLayer {
            self.data = layer.data
            self.reverse = layer.reverse
        } else {
            self.data = ExpressionData(leftEye: ExpressionItem(), rightEye: ExpressionItem(), mouth: ExpressionItem())
            self.reverse = false
        }
        super.init(layer: layer)
    }
    
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }
    
    override func draw(in ctx: CGContext) {
        let rect = self.frame
        
        // Background
        ctx.setFillColor(UIColor.black.withAlphaComponent(0.7).cgColor)
        ctx.fill(rect)
        
        let eyeSize = rect.width / 10
        
        func drawEye(_ item: ExpressionItem, at point: CGPoint) {
            
            // Calculate scale based on size (-100 to 100)
            // 0   -> 1.0 (keep current size)
            // -100 -> 0.5 (half normal radius)
            // 100  -> 2.0 (double normal radius)
            let clampedSize = max(-100, min(100, item.size))
            let sizeScale: CGFloat
            if clampedSize >= 0 {
                sizeScale = 1.0 + CGFloat(clampedSize) / 100.0
            } else {
                sizeScale = 1.0 + CGFloat(clampedSize) / 200.0
            }
            
            let scaledEyeSize = eyeSize * sizeScale
            
            let visibleHeight = scaledEyeSize * (CGFloat(item.weight) / 100)
            
            let centerX = point.x + CGFloat(item.x / 10) + eyeSize / 2
            let centerY = point.y + CGFloat(item.y / 10) + eyeSize / 2
            let eyeRect = CGRect(
                x: centerX - scaledEyeSize / 2,
                y: centerY - scaledEyeSize / 2,
                width: scaledEyeSize,
                height: scaledEyeSize
            )
            
            ctx.saveGState()
            
            // Rotation
            let rotationDegrees = CGFloat(item.rotation) / 10.0
            let center = CGPoint(x: eyeRect.midX, y: eyeRect.midY)
            ctx.translateBy(x: center.x, y: center.y)
            ctx.rotate(by: rotationDegrees * .pi / 180)
            ctx.translateBy(x: -center.x, y: -center.y)
            
            // Clip height
            let maskRect = CGRect(
                x: eyeRect.minX,
                y: eyeRect.maxY - visibleHeight,
                width: scaledEyeSize,
                height: visibleHeight
            )
            ctx.addRect(maskRect)
            ctx.clip()
            
            ctx.setFillColor(UIColor.white.cgColor)
            ctx.fillEllipse(in: eyeRect)
            
            ctx.restoreGState()
        }
        
        let eyeY = (rect.height * 0.4) - (eyeSize / 2)
        let leftEyePoint = CGPoint(x: (rect.width / 4) - (eyeSize / 2), y: eyeY)
        let rightEyePoint = CGPoint(x: (rect.width / 4 * 3) - (eyeSize / 2), y: eyeY)
        
        
        if reverse {
            // Temporarily swap rotation angles
            let leftEyeRotation = data.leftEye.rotation
            let rightEyeRotation = data.rightEye.rotation
            
            var leftEye = data.leftEye
            var rightEye = data.rightEye
            
            leftEye.rotation = rightEyeRotation
            rightEye.rotation = leftEyeRotation
            
            drawEye(leftEye, at: rightEyePoint)
            drawEye(rightEye, at: leftEyePoint)
        } else {
            drawEye(data.leftEye, at: leftEyePoint)
            drawEye(data.rightEye, at: rightEyePoint)
        }
        
        // Draw mouth
        ctx.saveGState()
        
        let width = rect.width * 0.3 - CGFloat(data.mouth.weight / 10)
        let height = 3 + CGFloat(data.mouth.weight) * 0.2
        let x = ((rect.width - width) / 2) + CGFloat(data.mouth.x / 10)
        let y = (rect.height * 0.65) + CGFloat(data.mouth.y / 10)
        
        let rotationDegrees = CGFloat(data.mouth.rotation) / 10.0
        let center = CGPoint(x: x + width / 2, y: y + height / 2)
        ctx.translateBy(x: center.x, y: center.y)
        ctx.rotate(by: rotationDegrees * .pi / 180)
        ctx.translateBy(x: -center.x, y: -center.y)
        
        let mouthRect = CGRect(x: x, y: y, width: width, height: height)
        let mouthPath = UIBezierPath(roundedRect: mouthRect, cornerRadius: height / 2)
        ctx.addPath(mouthPath.cgPath)
        ctx.setFillColor(UIColor.white.cgColor)
        ctx.fillPath()
        
        ctx.restoreGState()
    }
}

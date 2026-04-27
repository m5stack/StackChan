//
//  StackChanRotaryRobot.swift
//  Runner
//
//  Created by 袁智鸿 on 2026/1/30.
//
import SceneKit

class StackChanRotaryRobot: NSObject, FlutterPlatformView, FlutterStreamHandler {
    
    private let expressionLayer = ExpressionLayer(data: ExpressionData(leftEye: ExpressionItem(weight: 100), rightEye: ExpressionItem(weight: 100), mouth: ExpressionItem()))
    
    private let planeNodeName = "expressionPlane"
    
    private let sceneView: SCNView
    
    func view() -> UIView {
        return sceneView
    }
    
    func onListen(withArguments arguments: Any?, eventSink events: @escaping FlutterEventSink) -> FlutterError? {
        return nil
    }
    
    func onCancel(withArguments arguments: Any?) -> FlutterError? {
        return nil
    }
    
    init(
        frame: CGRect,
        viewId: Int64,
        messenger: FlutterBinaryMessenger,
        args: Any?
    ) {
        self.sceneView = SCNView(frame: frame)
        super.init()
        setupSceneView()
        setupInitialScene()
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
        scene.rootNode.position.z = scene.rootNode.position.z - 45
        
        let clampedPitch = max(0, min(900, 200))
        let pitchRatio = Float(clampedPitch) / 900.0
        let pitchAngle = -Float.pi / 2 * (1 + pitchRatio)
        scene.rootNode.eulerAngles.x = pitchAngle
        
        if let rootNode = scene.rootNode.childNodes.first {
            setupRobotHierarchy(rootNode: rootNode, scene: scene)
        }
        sceneView.scene = scene
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
        
        // 旋转
        let rotateAction = SCNAction.rotateBy(x: 0, y: CGFloat(2 * Double.pi), z: 0, duration: 5)
        let repeatAction = SCNAction.repeatForever(rotateAction)
        scene.rootNode.runAction(repeatAction)
    }
    
    private func addExpressionPlane(to head: SCNNode) {
        let plane = SCNPlane(width: 42, height: 32)
        let magnification: CGFloat = 5
        let size = CGSize(width: magnification * plane.width, height: magnification * plane.height)
        expressionLayer.frame = CGRect(origin: .zero, size: size)
        expressionLayer.setNeedsDisplay()
        let newImage = expressionRenderer().image { ctx in
            self.expressionLayer.render(in: ctx.cgContext)
        }
        let material = SCNMaterial()
        material.diffuse.contents = newImage
        plane.materials = [material]
        let planeNode = SCNNode(geometry: plane)
        planeNode.name = planeNodeName
        planeNode.position = head.position
        planeNode.position.z = planeNode.position.z - 4.5
        head.addChildNode(planeNode)
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


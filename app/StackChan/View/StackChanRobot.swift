/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI
import SceneKit
import Combine

struct StackChanRobot : UIViewRepresentable {
    
    var data: DanceData
    
    var allowsCameraControl: Bool = false
    
    @State private var expressionLayer = ExpressionLayer(data: ExpressionData(leftEye: ExpressionItem(weight: 100), rightEye: ExpressionItem(weight: 100), mouth: ExpressionItem()))
    
    private let planeNodeName = "expressionPlane"
    
    private let rotateKey = "autoRotate"
    
    func makeUIView(context: Context) -> SCNView {
        let sceneView = SCNView()
        
        if let scene = SCNScene(named: "stackChanModel.scn") {
            scene.rootNode.eulerAngles = SCNVector3Zero
            scene.rootNode.eulerAngles.x = -Float.pi / 2
            scene.rootNode.position.y = scene.rootNode.position.y + 25
            scene.rootNode.position.z = scene.rootNode.position.z - 35
            
            let plane = SCNPlane(width: 42, height: 32)
            let magnification: CGFloat = 5
            let size = CGSize(width: magnification * plane.width, height: magnification * plane.height)
            expressionLayer.frame = CGRect(origin: .zero, size: size)
            expressionLayer.setNeedsDisplay()
            
            let material = SCNMaterial()
            plane.materials = [material]
            let planeNode = SCNNode(geometry: plane)
            planeNode.name = planeNodeName
            planeNode.position = SCNVector3(0, -16, 0)
            planeNode.eulerAngles = SCNVector3(Float.pi / 2, 0, 0)
            scene.rootNode.addChildNode(planeNode)
            
            sceneView.scene = scene
        } else {
            print("Model not found")
        }
        
        sceneView.autoenablesDefaultLighting = true
        sceneView.allowsCameraControl = allowsCameraControl
        sceneView.backgroundColor = UIColor.clear
        setData(sceneView)
        return sceneView
    }
    
    func updateUIView(_ uiView: SCNView, context: Context) {
        setData(uiView)
    }
    
    /// Refresh model position and expression
    private func setData(_ uiView: SCNView) {
        if let stackNode = uiView.scene?.rootNode {
            /// Set pitch angle (0–900)
            let clampedPitch = max(0, min(900, data.pitchServo.angle))
            let pitchRatio = Float(clampedPitch) / 900.0
            let pitchAngle = -Float.pi / 2 * (1 + pitchRatio)
            stackNode.eulerAngles.x = pitchAngle
            
            // Cancel previous auto-rotation
            stackNode.removeAction(forKey: rotateKey)
            
            if data.yawServo.rotate == 0 {
                /// Set yaw angle (-128 to 128, left to right)
                let clampedYaw = max(-1280, min(1280, data.yawServo.angle))   // Clamp to -128~128
                let yawAngle = Float(clampedYaw) * Float.pi / 1800  // Convert to radians
                stackNode.eulerAngles.y = yawAngle
            } else {
                let rotateSpeed = max(-1000, min(1000, data.yawServo.rotate))
                let radiansPerSecond = Float(rotateSpeed) / 1000.0 * Float.pi * 2
                // Rotate continuously using angular velocity (not a fixed-loop animation)
                let rotateAction = SCNAction.customAction(duration: .infinity) { node, _ in
                    let deltaTime: Float = 1.0 / 60.0   // Approximate frame duration
                    node.eulerAngles.y += radiansPerSecond * deltaTime
                }
                stackNode.runAction(rotateAction, forKey: rotateKey)
            }
        }
        // Find the plane node
        if let planeNode = uiView.scene?.rootNode.childNode(withName: planeNodeName, recursively: true),
           let plane = planeNode.geometry as? SCNPlane {
            
            // Render new expression image
            let expressionData = ExpressionData(leftEye: data.leftEye, rightEye: data.rightEye, mouth: data.mouth)
            expressionLayer.data = expressionData
            expressionLayer.setNeedsDisplay()
            let newImage = expressionRenderer().image { ctx in
                self.expressionLayer.render(in: ctx.cgContext)
            }
            plane.firstMaterial?.diffuse.contents = newImage
        }
    }
    
    static func dismantleUIView(_ uiView: SCNView, coordinator: ()) {
        // Remove all nodes from the scene
        uiView.scene?.rootNode.childNodes.forEach { $0.removeFromParentNode() }
        
        // Clean up scene and materials
        uiView.scene = nil
        uiView.delegate = nil
        
        // Stop any rendering or animations
        uiView.isPlaying = false
        uiView.scene?.isPaused = true
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



struct StackChanRotaryRobot : UIViewRepresentable {
    
    private let expressionLayer = ExpressionLayer(data: ExpressionData(leftEye: ExpressionItem(weight: 100), rightEye: ExpressionItem(weight: 100), mouth: ExpressionItem()))
    
    func updateUIView(_ uiView: SCNView, context: Context) {
        
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
    
    func makeUIView(context: Context) -> SCNView {
        let sceneView = SCNView()
        
        if let scene = SCNScene(named: "stackChanModel.scn") {
            scene.rootNode.eulerAngles = SCNVector3Zero
            scene.rootNode.eulerAngles.x = -Float.pi / 2
            scene.rootNode.position.y = scene.rootNode.position.y + 25
            scene.rootNode.position.z = scene.rootNode.position.z - 45
            
            let clampedPitch = max(0, min(900, 200))
            let pitchRatio = Float(clampedPitch) / 900.0
            let pitchAngle = -Float.pi / 2 * (1 + pitchRatio)
            scene.rootNode.eulerAngles.x = pitchAngle
            
            // Add plane
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
            planeNode.position = SCNVector3(0, -16, 0)
            planeNode.eulerAngles = SCNVector3(Float.pi / 2, 0, 0)
            scene.rootNode.addChildNode(planeNode)
            
            // Add infinite rotation animation around Y axis
            let rotateAction = SCNAction.rotateBy(x: 0, y: CGFloat(2 * Double.pi), z: 0, duration: 5)
            let repeatAction = SCNAction.repeatForever(rotateAction)
            scene.rootNode.runAction(repeatAction)
            
            sceneView.scene = scene
        } else {
            print("Model not found")
        }
        
        sceneView.autoenablesDefaultLighting = true
        sceneView.allowsCameraControl = false
        sceneView.backgroundColor = UIColor.clear
        return sceneView
    }
    
    static func dismantleUIView(_ uiView: UIViewType, coordinator: ()) {
        // Remove all nodes from the scene
        uiView.scene?.rootNode.childNodes.forEach { $0.removeFromParentNode() }
        
        // Clean up scene and materials
        uiView.scene = nil
        uiView.delegate = nil
        
        // Stop any rendering or animations
        uiView.isPlaying = false
        uiView.scene?.isPaused = true
    }
}



struct StackChanRobotPreview : PreviewProvider {
    
    static var previews: some View {
        StackChanRotaryRobot()
            .frame(maxWidth: .infinity,maxHeight: 400)
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
        
        let eyeY = (rect.height * 0.35) - (eyeSize / 2)
        let leftEyePoint = CGPoint(x: (rect.width / 3) - (eyeSize / 2), y: eyeY)
        let rightEyePoint = CGPoint(x: (rect.width / 3 * 2) - (eyeSize / 2), y: eyeY)
        
        
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

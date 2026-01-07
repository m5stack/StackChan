/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI
import RealityKit
import ARKit

struct ARCameraView : UIViewRepresentable {
    
    @Binding var expressionData: ExpressionData
    
    @Binding var decorate: Int
    
    @Binding var captureScreen: Bool
    
    let onCallback : ((ARSession,[ARAnchor]) -> Void)?
    
    let onFrameCallback : ((UIImage) -> Void)?
    
    func makeUIView(context: Context) -> ARSCNView {
        let arView = ARSCNView(frame: .zero)
        arView.contentMode = .scaleAspectFit
        arView.autoresizingMask = [.flexibleWidth,.flexibleHeight]
        
        let configuration = ARFaceTrackingConfiguration()
        configuration.isLightEstimationEnabled = true
        //4K
        if let format = ARFaceTrackingConfiguration.supportedVideoFormats.last {
            configuration.videoFormat = format
        }
        //HDR
        configuration.videoHDRAllowed = true
        
        arView.automaticallyUpdatesLighting = true
        arView.session.delegate = context.coordinator
        arView.delegate = context.coordinator
        arView.session.run(configuration, options: [.resetTracking,.removeExistingAnchors])
        
        return arView
    }
    
    func updateUIView(_ uiView: ARSCNView, context: Context) {
        context.coordinator.updateDecoration(decorate: decorate, uiView, context: context, expressionData: expressionData)
    }
    
    func makeCoordinator() -> Corrdinator {
        Corrdinator(parent: self)
    }
    
    //robot data
    var robot: ExpressionData = ExpressionData(leftEye: ExpressionItem(), rightEye: ExpressionItem(), mouth: ExpressionItem())
    
    static func dismantleUIView(_ uiView: ARSCNView, coordinator: Corrdinator) {
        uiView.session.pause()
        uiView.delegate = nil
        uiView.session.delegate = nil
        uiView.isPlaying = false
        uiView.scene.rootNode.enumerateChildNodes { node, _ in
            node.removeFromParentNode()
        }
        uiView.scene = SCNScene()
    }
    
    class Corrdinator: NSObject, ARSessionDelegate , ARSCNViewDelegate {
        var parent: ARCameraView
        
        var decorate: Int = 0
        
        var expressionLayer = ExpressionLayer(data: ExpressionData(leftEye: ExpressionItem(), rightEye: ExpressionItem(), mouth: ExpressionItem()),reverse: true)
        
        var faceAnchorNode: SCNNode?
        var currentDecorationNode: SCNNode?
        
        private var lastCaptureTime: TimeInterval = 0
        
        init(parent: ARCameraView) {
            self.parent = parent
        }
        
        func session(_ session: ARSession, didUpdate anchors: [ARAnchor]) {
            parent.onCallback?(session,anchors)
        }
        
        func renderer(_ renderer: any SCNSceneRenderer, updateAtTime time: TimeInterval) {
            if parent.captureScreen {
                if time - lastCaptureTime >= 0.5 {
                    lastCaptureTime = time
                    guard let scnView = renderer as? ARSCNView else { return }
                    let renderedImage = scnView.snapshot()
                    parent.onFrameCallback?(renderedImage)
                }
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
        
        func createEmojiNoseNode(emoji: String) -> SCNNode {
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
        
        func createStackChanModel() -> SCNNode {
            guard let scene = SCNScene(named: "stackChanModel.scn"),
                  let modelNode = scene.rootNode.childNodes.first else {
                print("no model")
                return SCNNode()
            }
            modelNode.name = "stackChanModel"
            modelNode.scale = SCNVector3(0.004, 0.004, 0.004)
            modelNode.opacity = 0.4
            modelNode.position = SCNVector3(0, 0.03, 0)
            modelNode.eulerAngles = SCNVector3Zero
            modelNode.eulerAngles.x = -Float.pi / 2
            
            return modelNode
        }
        
        func renderer(_ renderer: SCNSceneRenderer, nodeFor anchor: ARAnchor) -> SCNNode? {
            guard anchor is ARFaceAnchor else { return nil }
            
            let node = SCNNode()
            self.faceAnchorNode = node
            
            updateDecorationOnNode(node: node, decorate: decorate)
            
            return node
        }
        
        func createPlane() -> SCNNode {
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
        
        func updateDecorationOnNode(node: SCNNode, decorate: Int) {
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
        
        func updateDecoration(decorate: Int,_ uiView: ARSCNView, context: Context, expressionData: ExpressionData) {
            DispatchQueue.main.async {
                if self.decorate != decorate {
                    self.decorate = decorate
                    if let faceNode = self.faceAnchorNode {
                        self.updateDecorationOnNode(node: faceNode, decorate: decorate)
                    }
                }
                
                if decorate == 1 {
                    let scene = uiView.scene
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
    }
}


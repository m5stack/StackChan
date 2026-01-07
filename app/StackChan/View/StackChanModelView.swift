/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI
import ARKit

struct StackChanModelView: View {
    
    @Binding var expressionData: ExpressionData
    @Binding var headData: MotionData
    
    var body: some View {
        Canvas { context, size in
            let eyeSize = size.width / 10
            
            func drawEye(_ item: ExpressionItem, at point: CGPoint) {
                let visibleHeight = eyeSize * (CGFloat(item.weight) / 100)
                
                let eyeX = point.x + CGFloat(item.x / 10)
                let eyeY = point.y + CGFloat(item.y / 10)
                
                let eyeRect = CGRect(x: eyeX, y: eyeY, width: eyeSize, height: eyeSize)
                var eyePath = Path()
                eyePath.addEllipse(in: eyeRect)
                
                let rotationDegrees = Double(item.rotation) / 10.0
                let rotationAngle = Angle(degrees: rotationDegrees)
                
                let maskRect = CGRect(
                    x: eyeX,
                    y: eyeY + eyeSize - visibleHeight,
                    width: eyeSize,
                    height: visibleHeight
                )
                
                context.drawLayer { context in
                    let center = CGPoint(x: eyeRect.midX, y: eyeRect.midY)
                    context.translateBy(x: center.x, y: center.y)
                    context.rotate(by: rotationAngle)
                    context.translateBy(x: -center.x, y: -center.y)
                    
                    context.clip(to: Path(maskRect))
                    context.fill(
                        Path(ellipseIn: eyeRect),
                        with: .color(.white)
                    )
                }
            }
            
            
            let eyeY = (size.height * 0.35) - (eyeSize / 2)
            let leftEyePoint = CGPoint(x: (size.width / 3) - (eyeSize / 2) ,y: eyeY)
            let rightEyePoint = CGPoint(x: (size.width / 3 * 2) - (eyeSize / 2) ,y: eyeY)
            
            drawEye(expressionData.leftEye, at: leftEyePoint)
            drawEye(expressionData.rightEye, at: rightEyePoint)
            
            
            context.drawLayer { context in
                
                let width = size.width * 0.3 - CGFloat(expressionData.mouth.weight / 10)
                let height = 3 + CGFloat(expressionData.mouth.weight) * 0.2
                let x = ((size.width - width) / 2) + CGFloat(expressionData.mouth.x / 10)
                let y = (size.height * 0.65) + CGFloat(expressionData.mouth.y / 10)
                
                let rotationDegrees = Double(expressionData.mouth.rotation) / 10.0
                let rotationAngle = Angle(degrees: rotationDegrees)
                
                let mouthRect = CGRect(x: x, y: y, width: width, height: height)
                let mouthPath = Path(roundedRect: mouthRect, cornerRadius: height / 2)
                
                let center = CGPoint(x: mouthRect.midX, y: mouthRect.midY)
                context.translateBy(x: center.x, y: center.y)
                context.rotate(by: rotationAngle)
                context.translateBy(x: -center.x, y: -center.y)
                
                context.fill(mouthPath, with: .color(.white))
            }
        }
    }
}








struct SceneKitView: UIViewRepresentable {
    @Binding var expressionData: ExpressionData
    
    private let planeNodeName = "expressionPlane"
    
    @State var expressionLayer = ExpressionLayer(data: ExpressionData(leftEye: ExpressionItem(), rightEye: ExpressionItem(), mouth: ExpressionItem()))
    
    func makeUIView(context: Context) -> SCNView {
        let scnView = SCNView()
        let scene = SCNScene()
        scnView.scene = scene
        scnView.allowsCameraControl = true
        scnView.autoenablesDefaultLighting = true
        scnView.backgroundColor = .clear
        
        expressionLayer.data = expressionData
        expressionLayer.frame = CGRect(origin: .zero, size: CGSize(width: 250, height: 200))
        expressionLayer.setNeedsDisplay()
        
        DispatchQueue.main.async {
            let plane = SCNPlane(width: 0.08, height: 0.06)
            let material = SCNMaterial()
            material.diffuse.contents = expressionLayer
            material.isDoubleSided = true
            plane.materials = [material]
            
            let planeNode = SCNNode(geometry: plane)
            planeNode.name = planeNodeName
            
            var position = SCNVector3()
            position.x += 0.02
            position.y += 0.015
            position.z += 0.01
            
            planeNode.position = position
            
            scene.rootNode.addChildNode(planeNode)
            
            if let position = scnView.scene?.rootNode.position {
                scnView.scene?.rootNode.position.z = position.z - 0.03
            }
        }
        
        return scnView
    }
    
    func updateUIView(_ uiView: SCNView, context: Context) {
        guard let scene = uiView.scene,
              let planeNode = scene.rootNode.childNode(withName: planeNodeName, recursively: true),
              let plane = planeNode.geometry as? SCNPlane,
              let material = plane.materials.first else {
            return
        }
        expressionLayer.data = expressionData
        expressionLayer.setNeedsDisplay()
        material.diffuse.contents = expressionLayer
    }
    
    
}


struct SceneKitViewPreview : PreviewProvider {
    
    static var previews: some View {
        SceneKitView(
            expressionData: .constant(
                ExpressionData(leftEye: ExpressionItem(), rightEye: ExpressionItem(), mouth: ExpressionItem())
            )
        )
        .frame(maxWidth: 400,maxHeight: 400)
    }
}

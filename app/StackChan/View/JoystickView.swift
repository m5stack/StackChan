/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI

struct JoystickView: View {
    
    @State private var dragOffset: CGSize = .zero
    
    let callback: ((_ radians: CGFloat,_ strength: CGFloat) -> Void)?
    
    @State private var isDragging: Bool = false
    
    var body: some View {
        GeometryReader { proxy in
            let diameter = min(proxy.size.width, proxy.size.height)
            let radius = diameter / 2
            let joystickDiameter = diameter / 4
            let stickRadius = joystickDiameter / 2
            let lineWidth: CGFloat = 4
            let maxRadius = radius - stickRadius - (lineWidth / 2)
            ZStack {
                Circle()
                    .stroke(Color(UIColor.separator), lineWidth: lineWidth)
                    .frame(width: diameter,height: diameter)
                Circle()
                    .fill(Color.accent)
                    .frame(width: joystickDiameter,height: joystickDiameter)
                    .glassEffectCircle()
                    .offset(dragOffset)
            }
            .contentShape(Circle())
            .highPriorityGesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in
                        withAnimation {
                            isDragging = true
                        }
                        let dx = value.location.x - radius
                        let dy = value.location.y - radius
                        let distance = sqrt(dx * dx + dy * dy)
                        if distance <= maxRadius {
                            dragOffset = CGSize(width: dx, height: dy)
                        } else {
                            let angle = atan2(dy, dx)
                            dragOffset = CGSize(
                                width: cos(angle) * maxRadius,
                                height: sin(angle) * maxRadius
                            )
                        }
                    }
                    .onEnded { _ in
                        withAnimation {
                            isDragging = false
                            dragOffset = .zero
                        }
                    },
                including: .all
            )
            .padding(0)
            .onChange(of: dragOffset) { newValue in
                guard isDragging else { return }
                let dx = newValue.width
                let dy = newValue.height
                let distance = sqrt(dx * dx + dy * dy)
                let radians = atan2(dy, dx)
                // 直接使用之前定义的 maxRadius
                let strength = min(distance / maxRadius, 1)
                callback?(radians,strength)
            }
        }
    }
}

struct JoystickViewPreview : PreviewProvider {
    static var previews: some View {
        JoystickView { radians, strength in
            print(radians)
        }
    }
}

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI

struct RippleDiffusion<Content: View> : View {
    
    let content: () -> Content
    
    init(@ViewBuilder content: @escaping () -> Content) {
        self.content = content
    }
    
    @State private var animate = false
    
    var body: some View {
        ZStack(alignment: .center) {
            ForEach(0..<3) { index in
                Circle()
                    .stroke(Color.accentColor.opacity(0.7), lineWidth: 2)
                    .frame(width: CGFloat(index + 1) * 100, height: CGFloat(index + 1) * 100)
                    .scaleEffect(animate ? 2.0 : 0.1)
                    .opacity(animate ? 0 : 1)
                    .animation(
                        Animation.easeOut(duration: 1.8)
                            .repeatForever(autoreverses: false)
                            .delay(Double(index) * 0.3),
                        value: animate
                    )
            }
            content()
        }
        .onAppear {
            DispatchQueue.main.async {
                animate = true
            }
        }
    }
}


struct RippleDiffusionPreview : PreviewProvider {
    static var previews: some View {
        RippleDiffusion {
            
        }
    }
}

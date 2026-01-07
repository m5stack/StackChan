/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI

struct DazzlingBackground : View {
    
    let backColors: [Color]
    let background: Color
    let dotCount: Int
    let speed: CGFloat
    
    @State private var dots: [Dot] = []
    
    init(backColors: [Color], background: Color, dotCount: Int = 5, speed: CGFloat = 2.2) {
        self.backColors = backColors
        self.background = background
        self.dotCount = dotCount
        self.speed = speed
    }
    
    var body: some View {
        GeometryReader { proxy in
            ZStack {
                LinearGradient(
                    colors: backColors,
                    startPoint: .topLeading,
                    endPoint: .bottomTrailing
                )
                TimelineView(.animation) { timeline in
                    Canvas { context, size in
                        for dot in dots {
                            let circle = Path(ellipseIn: CGRect(x: dot.position.x, y: dot.position.y, width: dot.dotSize, height: dot.dotSize))
                            context.fill(circle, with: .color(.purple.opacity(0.4)))
                        }
                    }
                    .blur(radius: 50)
                    .drawingGroup()
                    .onAppear {
                        DispatchQueue.main.async {
                            if dots.isEmpty, proxy.size.width > 0, proxy.size.height > 0 {
                                startDots(size: proxy.size)
                            }
                        }
                    }
                    .onChange(of: timeline.date) { _ in
                        DispatchQueue.main.async {
                            updateDots(size: proxy.size)
                        }
                    }
                }
            }
            .background(background)
        }
        
    }
    
    private func startDots(size: CGSize) {
        guard size.width > 0, size.height > 0 else { return }
        for _ in 0..<dotCount {
            let pos = CGPoint(x: CGFloat.random(in: 0..<size.width), y: CGFloat.random(in: 0..<size.height))
            let target =  CGPoint(x: CGFloat.random(in: 0..<size.width), y: CGFloat.random(in: 0..<size.height))
            let dotSize: CGFloat = CGFloat.random(in: 200...300)
            dots.append(Dot(position: pos, target: target,dotSize: dotSize))
        }
    }
    
    private func updateDots(size: CGSize) {
        guard size.width > 0, size.height > 0 else { return }
        for i in dots.indices {
            var dot = dots[i]
            let dx = dot.target.x - dot.position.x
            let dy = dot.target.y - dot.position.y
            let distance = sqrt(dx*dx + dy*dy)
            if distance < speed {
                dot.target = CGPoint(x: CGFloat.random(in: 0..<size.width), y: CGFloat.random(in: 0..<size.height))
            } else {
                dot.position.x += dx / distance * speed
                dot.position.y += dy / distance * speed
            }
            dots[i] = dot
        }
    }
}

struct Dot {
    var position: CGPoint
    var target: CGPoint
    var dotSize: CGFloat
}

struct DazzlingBackgroundPreview : PreviewProvider {
    
    
    static var previews: some View {
        DazzlingBackground(backColors: [Color.accent.opacity(0.5), Color.pink.opacity(0.2),Color.blue.opacity(0.5)],background: .white)
            .ignoresSafeArea()
    }
}

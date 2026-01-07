/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI


struct WideOrangeButton: ButtonStyle {
    
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .padding(.all,15)
            .frame(maxWidth: .infinity,minHeight: 44)
            .foregroundColor(.white)
            .background(RoundedRectangle(cornerRadius: 20.0)
                .fill(.blue)
            )
            .shadow(color: .black.opacity(0.2), radius: 5, x: 0, y: 0)
            .opacity(configuration.isPressed ? 0.6 : 1.0)
    }
}

struct MyTextFieldStyle : TextFieldStyle {
    func _body(configuration: TextField<Self._Label>) -> some View {
        configuration
            .padding(.all,15)
            .frame(maxWidth: .infinity,minHeight: 44)
            .background(
                RoundedRectangle(cornerRadius: 20.0)
                    .fill(.background)
            )
            .shadow(color: .black.opacity(0.2), radius: 5, x: 0, y: 0)
    }
}

struct SopDirectoryButtonStyle: ButtonStyle {
    let select: Bool
    
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .padding(.all, 20)
            .frame(maxWidth: .infinity, minHeight: 44)
            .foregroundColor(select ? .accentColor : .primary)
            .background(
                RoundedRectangle(cornerRadius: 20)
                    .fill(select ? Color.blue.opacity(0.2) : Color.clear)
            )
            .opacity(configuration.isPressed ? 0.6 : 1.0)
    }
}

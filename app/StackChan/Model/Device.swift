/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import Foundation

struct Device : Codable {
    var mac: String = UUID().uuidString
    var name: String? = nil
}

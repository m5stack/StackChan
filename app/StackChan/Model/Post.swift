/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import Foundation

struct Post : Codable{
    var id: Int
    var mac: String? = nil
    var name: String? = nil
    var contentText: String? = nil
    var contentImage: String? = nil
    var createdAt: String? = nil
    var postCommentList: [PostComment]? = nil
}

struct PostComment: Codable {
    var id: Int? = nil
    var postId: Int? = nil
    var mac: String? = nil
    var name: String? = nil
    var content: String? = nil
    var createAt: String? = nil
}


struct GetPostComment: Codable {
    var list: [PostComment]? = nil
    var total: Int? = nil
}

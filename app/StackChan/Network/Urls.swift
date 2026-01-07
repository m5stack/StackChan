/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

struct Urls {
    
    // Base URL configured according to the server's IP
    static let url = "192.168.51.43:12800/"
    
    static func getBaseUrl() -> String {
        return "http://" + url + "stackChan/"
    }
    
    static func getFileUrl() -> String {
        return "http://" + url
    }
    
    static func getWebSocketUrl() -> String {
        return "ws://" + url + "stackChan/ws"
    }
    
    static let registerMac = "api/v2/device/registerMac"
    
    static let dance = "dance"
    
    static let deviceRandomList = "device/randomList"
    
    static let uploadFile = "uploadFile"
    
    static let postAdd = "post/add"
    
    static let postGet = "post/get"
    
    static let postDelete = "post/delete"
    
    static let deviceInfo = "device/info"
    
    static let postCommentCreate = "post/comment/create"
    
    static let postCommentDelete = "post/comment/delete"
    
    static let postCommentGet = "post/comment/get"
}

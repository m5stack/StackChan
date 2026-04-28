/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import Foundation
import Flutter
import AVFoundation

class NativeBridge {
    static let shared = NativeBridge()
    
    private var channel: FlutterMethodChannel?
    private var audioPlayChannel: FlutterBasicMessageChannel?
    private weak var flutterViewController: FlutterViewController?
    
    private let channelName = "com.m5stack.stackchan/native"
    private let audioPlayChannelName = "com.m5stack.stackchan/audio_play"
    
    private var audioEngine: AVAudioEngine?
    private var audioPlayerNode: AVAudioPlayerNode?
    private let sampleRate: Double = 16000.0
    private let channels: AVAudioChannelCount = 1
    private var isAudioInitialized = false
    
    private let audioQueue = DispatchQueue(label: "com.stackchan.audio", qos: .userInitiated)
    private let audioFormat: AVAudioFormat? = AVAudioFormat(
        commonFormat: .pcmFormatFloat32,
        sampleRate: 16_000,
        channels: 1,
        interleaved: true
    )
    
    private init() {}
    
    func setup(with viewController: FlutterViewController) {
        self.flutterViewController = viewController
        let binaryMessenger = viewController.binaryMessenger
        
        channel = FlutterMethodChannel(name: channelName, binaryMessenger: binaryMessenger)
        audioPlayChannel = FlutterBasicMessageChannel(
            name: audioPlayChannelName,
            binaryMessenger: binaryMessenger,
            codec: FlutterBinaryCodec()
        )
        
        audioPlayChannel?.setMessageHandler { [weak self] message, reply in
            guard let self = self, let data = message as? Data else {
                reply(nil)
                return
            }
            self.audioQueue.async { [weak self] in
                self?.playAudio(pcmData: data)
            }
            reply(nil)
        }
    }
    
    private func playAudio(pcmData: Data) {
        guard let audioFormat = audioFormat else {
                        return
        }
        
        if !isAudioInitialized {
            guard setupAudioSession() else {
                                return
            }
            guard setupAudioEngine() else {
                                return
            }
            isAudioInitialized = true
                    }
        
        guard let engine = audioEngine, let playerNode = audioPlayerNode else {
            resetAudio()
            return
        }
        
        if !engine.isRunning {
            do {
                try engine.start()
            } catch {
                                resetAudio()
                return
            }
        }
        
        var floatBuffer = pcmData.withUnsafeBytes { (bytes: UnsafeRawBufferPointer) -> [Float] in
            let int16Buffer = bytes.bindMemory(to: Int16.self)
            var floats = [Float](repeating: 0, count: int16Buffer.count)
            //3
            for i in 0..<int16Buffer.count {
                floats[i] = min(max(Float(int16Buffer[i]) / Float(Int16.max) * 3.0, -1.0), 1.0)
            }
            return floats
        }
        
        guard let buffer = AVAudioPCMBuffer(pcmFormat: audioFormat,
                                            frameCapacity: AVAudioFrameCount(floatBuffer.count)) else { return }
        
        buffer.frameLength = buffer.frameCapacity
        memcpy(buffer.floatChannelData![0], &floatBuffer, floatBuffer.count * MemoryLayout<Float>.size)
        
        playerNode.scheduleBuffer(buffer)
        if !playerNode.isPlaying {
            playerNode.play()
        }
    }
    
    // MARK: - （-50）
    private func setupAudioSession() -> Bool {
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, mode: .default)
            try session.setActive(true)
            return true
        } catch {
            let nsError = error as NSError
                        return false
        }
    }
    
    // MARK: - （-10868）
    private func setupAudioEngine() -> Bool {
        guard let audioFormat = audioFormat else {
                        return false
        }
        
        let engine = AVAudioEngine()
        let playerNode = AVAudioPlayerNode()
        engine.attach(playerNode)
        
        // translated comment
        engine.connect(playerNode, to: engine.mainMixerNode, format: audioFormat)
        
        do {
            try engine.start()
        } catch {
                        return false
        }
        
        self.audioEngine = engine
        self.audioPlayerNode = playerNode
        return true
    }
    
    private func resetAudio() {
        audioPlayerNode?.stop()
        audioEngine?.stop()
        audioEngine = nil
        audioPlayerNode = nil
        isAudioInitialized = false
    }
    
    func stopPlayPCM() {
        audioQueue.async { [weak self] in
            self?.resetAudio()
        }
    }
    
    func sendMessage(method: Method,_ arguments: Any? = nil,_ completion: ((Any?) -> Void)? = nil) {
        guard method != .unknown else {
                        completion?(nil)
            return
        }
        channel?.invokeMethod(method.rawValue, arguments: arguments) { result in
            if let error = result as? FlutterError {
                            }
            completion?(result)
        }
    }
    
    func setMethodCallHandler(handler: @escaping FlutterMethodCallHandler) {
        channel?.setMethodCallHandler(handler)
    }
}

enum Method: String, CaseIterable {
    case wifiName
    case unknown
    case stopPlayPCM
    
    static func fromString(_ name: String) -> Method {
        return Method(rawValue: name) ?? .unknown
    }
}

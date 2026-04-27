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
            print("❌ 音频格式初始化失败")
            return
        }
        
        if !isAudioInitialized {
            guard setupAudioSession() else {
                print("❌ 会话初始化失败")
                return
            }
            guard setupAudioEngine() else {
                print("❌ 引擎初始化失败")
                return
            }
            isAudioInitialized = true
            print("✅ 音频初始化完成")
        }
        
        guard let engine = audioEngine, let playerNode = audioPlayerNode else {
            resetAudio()
            return
        }
        
        if !engine.isRunning {
            do {
                try engine.start()
            } catch {
                print("❌ 引擎启动失败: \(error)")
                resetAudio()
                return
            }
        }
        
        var floatBuffer = pcmData.withUnsafeBytes { (bytes: UnsafeRawBufferPointer) -> [Float] in
            let int16Buffer = bytes.bindMemory(to: Int16.self)
            var floats = [Float](repeating: 0, count: int16Buffer.count)
            //强制放大3倍
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
    
    // MARK: - 音频会话（无-50错误）
    private func setupAudioSession() -> Bool {
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, mode: .default)
            try session.setActive(true)
            return true
        } catch {
            let nsError = error as NSError
            print("❌ 音频会话错误：\(nsError.code) - \(nsError.localizedDescription)")
            return false
        }
    }
    
    // MARK: - 引擎初始化（修复-10868核心）
    private func setupAudioEngine() -> Bool {
        guard let audioFormat = audioFormat else {
            print("❌ 音频格式为空")
            return false
        }
        
        let engine = AVAudioEngine()
        let playerNode = AVAudioPlayerNode()
        engine.attach(playerNode)
        
        // 格式统一，不会崩溃
        engine.connect(playerNode, to: engine.mainMixerNode, format: audioFormat)
        
        do {
            try engine.start()
        } catch {
            print("❌ 引擎启动失败: \(error)")
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
            print("⚠️ 未知方法")
            completion?(nil)
            return
        }
        channel?.invokeMethod(method.rawValue, arguments: arguments) { result in
            if let error = result as? FlutterError {
                print("❌ 发送失败：\(error)")
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

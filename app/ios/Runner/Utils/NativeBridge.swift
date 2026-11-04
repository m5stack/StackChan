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
    private var recordEventChannel: FlutterEventChannel?
    private weak var flutterViewController: FlutterViewController?
    
    private let channelName = "com.m5stack.stackchan/native"
    private let audioPlayChannelName = "com.m5stack.stackchan/audio_play"
    private let recordChannelName = "com.m5stack.stackchan/record"
    
    private var audioEngine: AVAudioEngine?
    private var audioPlayerNode: AVAudioPlayerNode?
    fileprivate var recordEventSink: FlutterEventSink?
    private var recordConverter: AVAudioConverter?
    private var isRecording = false
    private var recordTapInstalled = false
    private var recordBufferCount = 0
    private let sampleRate: Double = 16000.0
    private let channels: AVAudioChannelCount = 1
    private var isAudioInitialized = false
    private var audioSessionConfigured = false
    private var audioSessionActive = false
    
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

        recordEventChannel = FlutterEventChannel(
            name: recordChannelName,
            binaryMessenger: binaryMessenger
        )
        recordEventChannel?.setStreamHandler(AudioRecordStreamHandler(bridge: self))
    }
    
    func startRecording(completion: ((Bool) -> Void)? = nil) {
        audioQueue.async { [weak self] in
            let started = self?.startRecordingOnQueue() ?? false
            DispatchQueue.main.async {
                completion?(started)
            }
        }
    }

    @discardableResult
    private func startRecordingOnQueue() -> Bool {
        guard !isRecording else { return true }
        guard setupAudioSession(),
              prepareAudioEngine(includeInput: true, explicitInputFormat: true) else {
            resetAudio()
            print("❌ 录音引擎初始化失败")
            return false
        }
        isRecording = true
        if startPreparedAudioEngine() {
            print("🎙️ iOS 录音已启动")
            return true
        }

        // A route/category transition can leave the input AU in an invalid format.
        // Rebuild the whole graph once instead of reusing the failed input chain.
        isRecording = false
        teardownAudioEngine()
        deactivateAudioSession()
        print("🔁 录音引擎重建后重试")
        guard setupAudioSession(),
              prepareAudioEngine(includeInput: true, explicitInputFormat: false) else {
            resetAudio()
            print("❌ 录音引擎重建失败")
            return false
        }
        isRecording = true
        guard startPreparedAudioEngine() else {
            isRecording = false
            resetAudio()
            print("❌ 录音引擎启动失败")
            return false
        }
        print("🎙️ iOS 录音已启动")
        return true
    }

    private func convertAndEmitRecordBuffer(_ buffer: AVAudioPCMBuffer) {
        guard isRecording else { return }
        recordBufferCount += 1
        if recordBufferCount == 1 || recordBufferCount % 50 == 0 {
            print("🎙️ iOS record buffer #\(recordBufferCount) frames=\(buffer.frameLength) format=\(buffer.format)")
        }
        let inputFormat = buffer.format
        guard inputFormat.sampleRate > 0,
              inputFormat.channelCount > 0,
              let targetFormat = AVAudioFormat(
                commonFormat: .pcmFormatFloat32,
                sampleRate: sampleRate,
                channels: 1,
                interleaved: true
              ) else {
            return
        }
        if recordConverter == nil {
            recordConverter = AVAudioConverter(from: inputFormat, to: targetFormat)
        }
        guard let converter = recordConverter else {
            print("❌ 录音格式转换初始化失败: input=\(inputFormat)")
            return
        }
        let ratio = targetFormat.sampleRate / inputFormat.sampleRate
        let capacity = AVAudioFrameCount(Double(buffer.frameLength) * ratio) + 64
        guard let output = AVAudioPCMBuffer(pcmFormat: targetFormat, frameCapacity: capacity) else {
            return
        }

        var suppliedInput = false
        var conversionError: NSError?
        let status = converter.convert(to: output, error: &conversionError) { _, outputStatus in
            if suppliedInput {
                outputStatus.pointee = .noDataNow
                return nil
            }
            suppliedInput = true
            outputStatus.pointee = .haveData
            return buffer
        }

        guard status == .haveData,
              conversionError == nil,
              output.frameLength > 0,
              let floats = output.floatChannelData?[0] else {
            return
        }
        let sampleCount = Int(output.frameLength)
        var samples = [Int16](repeating: 0, count: sampleCount)
        for index in 0..<sampleCount {
            let value = min(max(floats[index], -1.0), 1.0)
            samples[index] = Int16(value * Float(Int16.max))
        }
        let data = samples.withUnsafeBytes { Data($0) }
        DispatchQueue.main.async { [weak self] in
            self?.recordEventSink?(data)
        }
    }

    func stopRecording() {
        audioQueue.async { [weak self] in
            self?.stopRecordingOnQueue()
        }
    }

    private func stopRecordingOnQueue() {
        guard isRecording || recordTapInstalled else { return }
        resetAudio()
        print("🛑 iOS 录音已停止")
    }

    private func playAudio(pcmData: Data) {
        guard let audioFormat = audioFormat else {
            print("❌ 音频格式初始化失败")
            return
        }
        
        if audioEngine == nil || !isAudioInitialized {
            guard setupAudioSession() else {
                print("❌ 会话初始化失败")
                return
            }
            guard prepareAudioEngine(includeInput: isRecording) else {
                print("❌ 引擎初始化失败")
                return
            }
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
            // Scale up by 3x
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
    
    // MARK: - Audio session (avoids error -50)
    private func setupAudioSession() -> Bool {
        do {
            let session = AVAudioSession.sharedInstance()
            if !audioSessionConfigured ||
                session.category != .playAndRecord ||
                session.mode != .voiceChat {
                try session.setCategory(
                    .playAndRecord,
                    mode: .voiceChat,
                    options: [.defaultToSpeaker, .allowBluetooth]
                )
                audioSessionConfigured = true
            }
            if !audioSessionActive {
                try session.setActive(true)
                audioSessionActive = true
            }
            return true
        } catch {
            let nsError = error as NSError
            print("❌ 音频会话错误：\(nsError.code) - \(nsError.localizedDescription)")
            return false
        }
    }
    
    // MARK: - Engine setup (fixes error -10868)
    private func prepareAudioEngine(
        includeInput: Bool,
        explicitInputFormat: Bool = true
    ) -> Bool {
        teardownAudioEngine()
        guard let audioFormat = audioFormat else {
            print("❌ 音频格式为空")
            return false
        }
        
        let engine = AVAudioEngine()
        let playerNode = AVAudioPlayerNode()
        engine.attach(playerNode)
        
        // Use one consistent format so the engine cannot crash
        engine.connect(playerNode, to: engine.mainMixerNode, format: audioFormat)

        if includeInput {
            let inputNode = engine.inputNode
            let inputFormat = inputNode.outputFormat(forBus: 0)
            guard inputFormat.sampleRate > 0, inputFormat.channelCount > 0 else {
                print("❌ 录音输入格式无效: \(inputFormat)")
                return false
            }
            recordConverter = nil
            recordBufferCount = 0
            inputNode.installTap(
                onBus: 0,
                bufferSize: 1024,
                format: explicitInputFormat ? inputFormat : nil
            ) { [weak self] buffer, _ in
                self?.convertAndEmitRecordBuffer(buffer)
            }
            recordTapInstalled = true
        }

        self.audioEngine = engine
        self.audioPlayerNode = playerNode
        self.isAudioInitialized = true
        return true
    }

    private func startPreparedAudioEngine() -> Bool {
        guard let engine = audioEngine else { return false }
        engine.prepare()
        do {
            try engine.start()
            return true
        } catch {
            let nsError = error as NSError
            print("❌ 引擎启动失败: code=\(nsError.code) \(error)")
            return false
        }
    }

    private func teardownAudioEngine() {
        if let engine = audioEngine {
            audioPlayerNode?.stop()
            if engine.isRunning {
                engine.stop()
            }
            if recordTapInstalled {
                engine.inputNode.removeTap(onBus: 0)
            }
            engine.reset()
        }
        recordTapInstalled = false
        recordConverter = nil
        recordBufferCount = 0
        audioEngine = nil
        audioPlayerNode = nil
        isAudioInitialized = false
    }

    private func deactivateAudioSession() {
        guard audioSessionActive else { return }
        let session = AVAudioSession.sharedInstance()
        do {
            try session.setActive(false, options: .notifyOthersOnDeactivation)
        } catch {
            let nsError = error as NSError
            print("❌ 音频会话释放失败：\(nsError.code) - \(nsError.localizedDescription)")
        }
        audioSessionActive = false
        audioSessionConfigured = false
    }

    private func resetAudio() {
        isRecording = false
        teardownAudioEngine()
        deactivateAudioSession()
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

private final class AudioRecordStreamHandler: NSObject, FlutterStreamHandler {
    private weak var bridge: NativeBridge?

    init(bridge: NativeBridge) {
        self.bridge = bridge
    }

    func onListen(
        withArguments arguments: Any?,
        eventSink events: @escaping FlutterEventSink
    ) -> FlutterError? {
        bridge?.recordEventSink = events
        return nil
    }

    func onCancel(withArguments arguments: Any?) -> FlutterError? {
        bridge?.recordEventSink = nil
        bridge?.stopRecording()
        return nil
    }
}

enum Method: String, CaseIterable {
    case wifiName
    case unknown
    case stopPlayPCM
    case startRecording
    case stopRecording
    
    static func fromString(_ name: String) -> Method {
        return Method(rawValue: name) ?? .unknown
    }
}

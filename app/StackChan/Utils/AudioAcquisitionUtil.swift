/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI
import AVFoundation
import Accelerate

// Audio acquisition utility class, singleton pattern
class AudioAcquisitionUtil {
    
    static let shared = AudioAcquisitionUtil()
    private init() {}
    
    private let engine = AVAudioEngine()
    private let bus = 0
    var onAudioData: ((Data) -> Void)?
    var onDecibel: ((Float) -> Void)?
    
    func start() {
        let inputNode = engine.inputNode
        let hwFormat = inputNode.inputFormat(forBus: bus)
        inputNode.installTap(onBus: bus, bufferSize: 1024, format: hwFormat) { buffer, time in
            let audioBuffer = buffer.audioBufferList.pointee.mBuffers
            let data = Data(bytes: audioBuffer.mData!, count: Int(audioBuffer.mDataByteSize))
            
            // Callback with raw audio data
            DispatchQueue.main.async {
                self.onAudioData?(data)
            }
            
            // Calculate decibels and normalize
            if let floatChannelData = buffer.floatChannelData {
                let channelData = floatChannelData[0]
                let frameLength = vDSP_Length(buffer.frameLength)
                
                var rms: Float = 0
                vDSP_rmsqv(channelData, 1, &rms, frameLength) // Calculate RMS
                
                // Convert RMS to a 0-1 range, normal environment ~0
                // Here we assume the maximum RMS value could be around 0.1; adjust based on actual environment
                let normalizedDb = min(max(rms / 0.1, 0), 1)
                
                DispatchQueue.main.async {
                    self.onDecibel?(normalizedDb)
                }
            }
        }
        
        do {
            try engine.start()
        } catch {
            print("Audio engine start error: \(error)")
        }
    }
    
    func stop() {
        engine.inputNode.removeTap(onBus: bus)
        engine.stop()
    }
    
    deinit {
        stop()
    }
}

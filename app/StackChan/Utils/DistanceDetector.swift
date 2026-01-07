/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import ARKit
import AVFoundation

class DistanceDetector {
    

    private var arSession: ARSession?
    private var isDetectionActive = false
    private var distanceCallback: ((Float) -> Void)?
    private var thresholdCallback: (() -> Void)?
    private let thresholdDistance: Float = 0.05 // 5cm in meters
    private var timer: Timer?
    
    func startDistanceDetection(
        distanceUpdate: ((Float) -> Void)? = nil,
        belowThreshold: (() -> Void)? = nil
    ) {
        guard ARWorldTrackingConfiguration.isSupported else {
            return
        }
        if #available(iOS 13.0, *) {
            guard ARWorldTrackingConfiguration.supportsFrameSemantics(.sceneDepth) else {
                return
            }
        } else {
            return
        }
        
        checkCameraPermission { [weak self] granted in
            guard granted else {
                return
            }
            
            self?.setupARSession()
            self?.setupCallbacks(distanceUpdate: distanceUpdate, belowThreshold: belowThreshold)
            self?.startDetectionTimer()
        }
    }
    
    /// 停止距离检测
    func stopDistanceDetection() {
        isDetectionActive = false
        timer?.invalidate()
        timer = nil
        arSession?.pause()
        arSession = nil
    }
    
    // MARK: - Private Methods
    
    private func checkCameraPermission(completion: @escaping (Bool) -> Void) {
        let status = AVCaptureDevice.authorizationStatus(for: .video)
        
        switch status {
        case .authorized:
            completion(true)
        case .notDetermined:
            AVCaptureDevice.requestAccess(for: .video) { granted in
                DispatchQueue.main.async {
                    completion(granted)
                }
            }
        default:
            completion(false)
        }
    }
    
    private func setupARSession() {
        arSession = ARSession()
        let configuration = ARWorldTrackingConfiguration()
        
        if #available(iOS 13.0, *) {
            configuration.frameSemantics.insert(.sceneDepth)
        }
        
        arSession?.run(configuration)
        isDetectionActive = true
    }
    
    private func setupCallbacks(
        distanceUpdate: ((Float) -> Void)?,
        belowThreshold: (() -> Void)?
    ) {
        self.distanceCallback = distanceUpdate
        self.thresholdCallback = belowThreshold
    }
    
    private func startDetectionTimer() {
        timer?.invalidate()
        
        timer = Timer.scheduledTimer(
            timeInterval: 0.1,
            target: self,
            selector: #selector(performDistanceCheck),
            userInfo: nil,
            repeats: true
        )
    }
    
    @objc private func performDistanceCheck() {
        guard isDetectionActive,
              let frame = arSession?.currentFrame else {
            return
        }
        
        let distance = getCurrentDistance(from: frame)
        
        if let distance = distance {
            distanceCallback?(distance)
            
            if distance < thresholdDistance {
                handleBelowThreshold()
            }
        }
    }
    
    private func getCurrentDistance(from frame: ARFrame) -> Float? {
        if #available(iOS 13.0, *) {
            return getDistanceUsingSceneDepth(from: frame)
        } else {
            return getDistanceUsingHitTest(from: frame)
        }
    }
    
    @available(iOS 13.0, *)
    private func getDistanceUsingSceneDepth(from frame: ARFrame) -> Float? {
        guard let depthData = frame.sceneDepth else {
            return nil
        }
        
        let depthPixelBuffer = depthData.depthMap
        let width = CVPixelBufferGetWidth(depthPixelBuffer)
        let height = CVPixelBufferGetHeight(depthPixelBuffer)
        
        let centerX = width / 2
        let centerY = height / 2
        
        CVPixelBufferLockBaseAddress(depthPixelBuffer, .readOnly)
        
        guard let baseAddress = CVPixelBufferGetBaseAddress(depthPixelBuffer) else {
            CVPixelBufferUnlockBaseAddress(depthPixelBuffer, .readOnly)
            return nil
        }
        
        let floatBuffer = baseAddress.assumingMemoryBound(to: Float32.self)
        
        var totalDistance: Float = 0
        var validSamples = 0
        
        let sampleRadius = 5
        for x in max(0, centerX - sampleRadius)...min(width - 1, centerX + sampleRadius) {
            for y in max(0, centerY - sampleRadius)...min(height - 1, centerY + sampleRadius) {
                let distance = floatBuffer[y * width + x]
                if distance.isFinite && distance > 0 {
                    totalDistance += distance
                    validSamples += 1
                }
            }
        }
        
        CVPixelBufferUnlockBaseAddress(depthPixelBuffer, .readOnly)
        
        guard validSamples > 0 else {
            return nil
        }
        
        return totalDistance / Float(validSamples)
    }
    
    private func getDistanceUsingHitTest(from frame: ARFrame) -> Float? {
        return nil
    }
    
    private func handleBelowThreshold() {
        NSObject.cancelPreviousPerformRequests(withTarget: self, selector: #selector(executeThresholdCallback), object: nil)
    }
    
    @objc private func executeThresholdCallback() {
        let generator = UIImpactFeedbackGenerator(style: .heavy)
        generator.impactOccurred()
        AudioServicesPlaySystemSound(1013)
        thresholdCallback?()
    }
    
    deinit {
        stopDistanceDetection()
    }
}


func exampleBasicUsage() {
    let detector = DistanceDetector()

    detector.startDistanceDetection(
        distanceUpdate: { distance in
            let distanceInCm = distance * 100
            print(String(distanceInCm))
        },
        belowThreshold: {
        }
    )
}

class ProximityMonitor {
    private let detector = DistanceDetector()
    private var isMonitoring = false
    
    func startMonitoring() {
        detector.startDistanceDetection(
            distanceUpdate: { [weak self] distance in
                self?.handleDistanceUpdate(distance)
            },
            belowThreshold: { [weak self] in
                self?.handleProximityAlert()
            }
        )
        isMonitoring = true
    }
    
    func stopMonitoring() {
        detector.stopDistanceDetection()
        isMonitoring = false
    }
    
    private func handleDistanceUpdate(_ distance: Float) {
        let distanceInCm = distance * 100
        if distanceInCm < 10 {
        } else if distanceInCm < 30 {
        }
    }
    
    private func handleProximityAlert() {
        NotificationCenter.default.post(
            name: NSNotification.Name("ProximityAlert"),
            object: nil,
            userInfo: ["alert": "object_too_close"]
        )
    }
}

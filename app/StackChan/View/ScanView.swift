/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI
import AVFoundation

struct ScanView : UIViewControllerRepresentable {
    func updateUIViewController(_ uiViewController: ScannerViewController, context: Context) {}
    
    typealias UIViewControllerType = ScannerViewController
    
    typealias ScanCompletion = (Result<String,Error>) -> Void
    
    var completion: ScanCompletion
    
    func makeUIViewController(context: Context) -> ScannerViewController {
        let vc = ScannerViewController()
        vc.completion = completion
        return vc
    }
}

class ScannerViewController: UIViewController, AVCaptureMetadataOutputObjectsDelegate {
    var completion: ScanView.ScanCompletion?
    var captureSession: AVCaptureSession!
    var previewLayer: AVCaptureVideoPreviewLayer!
    
    // Flag to control whether callbacks are allowed
    private var isProcessing = false
    
    @objc private func toggleFlashlight() {
        guard let device = AVCaptureDevice.default(for: .video),
              device.hasTorch else { return }
        do {
            try device.lockForConfiguration()
            if device.torchMode == .on {
                device.torchMode = .off
            } else {
                try device.setTorchModeOn(level: 1.0)
            }
        } catch {
            print("Failed to toggle flashlight: \(error)")
        }
    }
    
    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = UIColor.black
        
        // Initialize guide view
        let guideImageView = UIImageView(image: UIImage(systemName: "viewfinder"))
        guideImageView.tintColor = .white
        guideImageView.contentMode = .scaleAspectFit
        guideImageView.tag = 998
        
        // Add breathing animation
        let pulse = CABasicAnimation(keyPath: "transform.scale")
        pulse.fromValue = 0.9
        pulse.toValue = 1.1
        pulse.duration = 0.7
        pulse.autoreverses = true
        pulse.repeatCount = .infinity
        guideImageView.layer.add(pulse, forKey: "breathingAnimation")
        
        view.addSubview(guideImageView)
        
        // Initialize flashlight button
        let flashlightButton = UIButton(type: .system)
        flashlightButton.setImage(UIImage(systemName: "flashlight.off.fill"), for: .normal)
        flashlightButton.tintColor = .white
        flashlightButton.addTarget(self, action: #selector(toggleFlashlight), for: .touchUpInside)
        flashlightButton.tag = 999
        view.addSubview(flashlightButton)
        
        switch AVCaptureDevice.authorizationStatus(for: .video) {
        case .authorized:
            setupSession()
        case .notDetermined:
            AVCaptureDevice.requestAccess(for: .video) { granted in
                DispatchQueue.main.async {
                    if granted {
                        self.setupSession()
                    } else {
                        self.completion?(.failure(NSError(domain: "Camera access not authorized", code: 0)))
                    }
                }
            }
        default:
            completion?(.failure(NSError(domain: "Camera access not authorized", code: 0)))
        }
    }
    
    private func setupSession() {
        captureSession = AVCaptureSession()
        captureSession.sessionPreset = .photo
        guard let videoCaptureDevice = AVCaptureDevice.default(for: .video),
              let videoInput = try? AVCaptureDeviceInput(device: videoCaptureDevice),
              captureSession.canAddInput(videoInput)
        else {
            completion?(.failure(NSError(domain: "Failed to initialize camera", code: 0)))
            return
        }
        captureSession.addInput(videoInput)
        
        let metadataOutput = AVCaptureMetadataOutput()
        guard captureSession.canAddOutput(metadataOutput) else {
            completion?(.failure(NSError(domain: "Unable to add capture output", code: 0)))
            return
        }
        captureSession.addOutput(metadataOutput)
        metadataOutput.setMetadataObjectsDelegate(self, queue: DispatchQueue.main)
        metadataOutput.metadataObjectTypes = [.qr, .ean13, .code128]
        
        previewLayer = AVCaptureVideoPreviewLayer(session: captureSession)
        previewLayer.videoGravity = .resizeAspectFill
        view.layer.insertSublayer(previewLayer, at: 0)
        
        DispatchQueue.global(qos: .userInitiated).async {
            self.captureSession.startRunning()
        }
    }
    
    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        previewLayer?.frame = view.bounds
        
        if let connection = previewLayer?.connection, connection.isVideoOrientationSupported {
            let deviceOrientation = UIDevice.current.orientation
            
            switch deviceOrientation {
            case .portrait:
                connection.videoOrientation = .portrait
            case .portraitUpsideDown:
                connection.videoOrientation = .portraitUpsideDown
            case .landscapeLeft:
                connection.videoOrientation = .landscapeRight   // Note: device left equals camera right
            case .landscapeRight:
                connection.videoOrientation = .landscapeLeft    // Note: device right equals camera left
            default:
                connection.videoOrientation = .portrait
            }
        }
        
        let guideImageViewSize: CGFloat = min(view.bounds.width, view.bounds.height) / 2
        let buttonSize: CGFloat = 44
        
        if let guideImageView = view.viewWithTag(998) as? UIImageView {
            guideImageView.frame = CGRect(
                x: (view.bounds.width - guideImageViewSize) / 2,
                y: (view.bounds.height - guideImageViewSize) / 2,
                width: guideImageViewSize,
                height: guideImageViewSize
            )
        }
        
        if let flashlightButton = view.viewWithTag(999) as? UIButton {
            var targetX = CGFloat(0)
            var targetY = CGFloat(0)
            
            if view.bounds.width > view.bounds.height {
                // Landscape
                let guideRightX = (view.bounds.width + guideImageViewSize) / 2
                let rightEdgeX = view.bounds.width - buttonSize
                targetX = (guideRightX + rightEdgeX) / 2
                targetY = (view.bounds.height / 2) - (buttonSize / 2)
            } else {
                // Portrait
                let guideBottomY = (view.bounds.height + guideImageViewSize) / 2
                let bottomEdgeY = view.bounds.height - buttonSize
                targetX = (view.bounds.width - buttonSize) / 2
                targetY = (guideBottomY + bottomEdgeY) / 2
            }
            flashlightButton.frame = CGRect(
                x: targetX,
                y: targetY,
                width: buttonSize,
                height: buttonSize
            )
        }
    }
    
    func metadataOutput(_ output: AVCaptureMetadataOutput, didOutput metadataObjects: [AVMetadataObject], from connection: AVCaptureConnection) {
        guard !isProcessing else { return }
        isProcessing = true // Mark as processing to avoid duplicate triggers
        
        if let metadataObject = metadataObjects.first as? AVMetadataMachineReadableCodeObject,
           let code = metadataObject.stringValue {
            AudioServicesPlaySystemSound(SystemSoundID(1057))
            completion?(.success(code))
            // Do not stop captureSession immediately
            // Call stopScanning() after external processing is finished
        } else {
            completion?(.failure(NSError(domain: "No QR code detected", code: 0)))
            isProcessing = false // Allow next scan
        }
    }
    
    // Provide a method for external callers to stop scanning
    func stopScanning() {
        if captureSession.isRunning {
            captureSession.stopRunning()
        }
        isProcessing = false
    }
    
    override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear(animated)
        stopScanning()
    }
}

import Flutter
import NetworkExtension
import CoreLocation
import UIKit

class SceneDelegate: FlutterSceneDelegate,CLLocationManagerDelegate {
    
    private let locationManager = CLLocationManager()
    
    override func scene(_ scene: UIScene, willConnectTo session: UISceneSession, options connectionOptions: UIScene.ConnectionOptions) {
        if let controller = window?.rootViewController as? FlutterViewController {
            NativeBridge.shared.setup(with: controller)
            NativeBridge.shared.setMethodCallHandler { [weak self] call, result in
                self?.handleMethodCall(call: call, result: result)
            }
            registerNativeViews(with: controller)
        }
        locationManager.delegate = self
    }
    
    private func registerNativeViews(with controller: FlutterViewController) {
        guard let registrar = controller.registrar(forPlugin: "stack_chan") else {
            return
        }
        let robotFactory =
        StackChanRobotViewFactory(messenger: controller.binaryMessenger)
        registrar.register(robotFactory, withId: "stackchan_robot_view")
        
        let rotaryFactory =
        StackChanRotaryRobotViewFactory(messenger: controller.binaryMessenger)
        registrar.register(rotaryFactory, withId: "stackchan_rotary_robot_view")
        
        let arViewFactory = StackChanArViewFactory(messenger: controller.binaryMessenger)
        registrar.register(arViewFactory, withId: "stackchan_ar_view")
    }
    
    private func handleMethodCall(call: FlutterMethodCall, result: @escaping FlutterResult) {
        let method = Method.fromString(call.method)
        switch method {
        case .wifiName:
            getWifiName()
        case .stopPlayPCM:
            NativeBridge.shared.stopPlayPCM()
        default:
            break
        }
    }
    
    private func getWifiName() {
        switch locationManager.authorizationStatus {
        case .authorizedWhenInUse, .authorizedAlways:
            fetchWifiInfo()
            break
        case .denied, .restricted:
            break
        case .notDetermined:
            locationManager.requestWhenInUseAuthorization()
            break
        default:
            break
        }
    }
    
    private func fetchWifiInfo() {
        NEHotspotNetwork.fetchCurrent { network in
            if let wifiName = network?.ssid {
                NativeBridge.shared.sendMessage(method: .wifiName, wifiName)
            }
        }
    }
    
    func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        print(manager.authorizationStatus)
        if manager.authorizationStatus == .authorizedWhenInUse ||
            manager.authorizationStatus == .authorizedAlways {
            fetchWifiInfo()
        }
    }
}

/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/cupertino.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/model/expression_data.dart';
import 'package:stack_chan/util/value_constant.dart';
import 'package:stack_chan/view/app.dart';
import 'package:stack_chan/view/popup/device_wifi_config.dart';

import '../model/blue_device_info.dart';
import '../model/dance_list.dart';

class BlueUtil {
  static final BlueUtil shared = BlueUtil._internal();

  BlueUtil._internal() {
    _initialize();
  }

  static const String danceTargetServiceUUID =
      "e2e5e5e0-1234-5678-1234-56789abcdef0";

  //MARK: - Core constants (align with iOS)
  static const String targetServiceUUID =
      "e2e5e5ff-1234-5678-1234-56789abcdef0";
  static const String headCharacteristicUUID =
      "0000ffe1-0000-1000-8000-00805f9b34fb";
  static const String wifiSetCharacteristicUUID =
      "e2e5e5e3-1234-5678-1234-56789abcdef0";
  static const String expressionCharacteristicUUID =
      "0000ffe3-0000-1000-8000-00805f9b34fb";
  static const String writeCharacteristicUUID =
      "0000ffe4-0000-1000-8000-00805f9b34fb";

  //MARK: - Core properties (align with iOS)
  List<BlueDeviceInfo> discoveredDevices = [];
  bool blueSwitch = false;
  final bool autoReconnect = true;
  BluetoothDevice? currentPeripheral;

  //Auto scan enabled by default
  bool automaticScanning = true;

  //Feature object
  BluetoothCharacteristic? writeCharacteristic;
  BluetoothCharacteristic? writeExpressionCharacteristic;
  BluetoothCharacteristic? writeHeadCharacteristic;
  BluetoothCharacteristic? writeWifiSetCharacteristic;

  //MARK: - Callback closures
  Function(List<BlueDeviceInfo>)? blufDevicesMonitoring;
  Function(BluetoothAdapterState)? centralManagerDidUpdateState;
  Function(BluetoothDevice, BluetoothCharacteristic)? characteristicCallback;
  Function(BluetoothDevice, bool)? connectionStateChanged;
  Function(List<int>)? wifiSetCharacteristicCall;

  //MARK: - Private properties
  StreamSubscription? _scanSubscription;
  StreamSubscription<BluetoothAdapterState>? _adapterStateSubscription;
  StreamSubscription<BluetoothConnectionState>? _connectionStateSubscription;
  Timer? _cleanupTimer;
  final Duration _deviceTimeout = const Duration(seconds: 3);

  static const String motionCharacteristicUUID =
      "e2e5e5e1-1234-5678-1234-56789abcdef0";
  static const String avatarCharacteristicUUID =
      "e2e5e5e2-1234-5678-1234-56789abcdef0";
  static const String configCharacteristicUUID =
      "e2e5e5e3-1234-5678-1234-56789abcdef0";
  static const String rgbCharacteristicUUID =
      "e2e5e5e4-1234-5678-1234-56789abcdef0";
  BluetoothCharacteristic? writeMotionCharacteristic;
  BluetoothCharacteristic? writeAvatarCharacteristic;
  BluetoothCharacteristic? writeRGBCharacteristic;

  int blueMode = 1; //1 WiFi mode  2 Dance mode 3 Pairing mode

  ///Cache scanned device list for change comparison
  List<String> cachedDeviceMacs = [];

  //MARK: - Initialization (fix Android first timing issue: permission → listen → enable Bluetooth)
  void _initialize() {
    //[Fix] Request permission first, init listener and Bluetooth after permission granted
    _requestBluetoothPermissions();
  }

  //MARK: - Request Bluetooth permission (permission_handler)
  Future<void> _requestBluetoothPermissions() async {
    try {
      Map<Permission, PermissionStatus> statuses;

      if (Platform.isAndroid) {
        //Android 12+ permissions
        statuses = await [
          Permission.bluetooth,
          Permission.bluetoothScan,
          Permission.bluetoothConnect,
          Permission.location,
        ].request();
      } else if (Platform.isIOS) {
        //iOS permissions
        statuses = await [
          Permission.bluetooth,
          Permission.bluetoothScan,
          Permission.bluetoothConnect,
          Permission.location,
        ].request();
      } else {
        return;
      }

      bool allGranted = true;
      statuses.forEach((permission, status) {
        if (!status.isGranted) {
          allGranted = false;
        }
      });

      //Execute enable Bluetooth command regardless of success to fix some plugin bugs
      if (allGranted) {
        debugPrint(" BluetoothPermissionAllGetSuccess");
        _registerBluetoothStateListener();
        await _tryTurnOnBluetooth();
      } else {
        debugPrint(" Part/AllBluetoothPermission被Reject,No法打开Bluetooth");
        _registerBluetoothStateListener();
        await _tryTurnOnBluetooth();
      }
    } catch (e) {
      debugPrint(" Permission申请Exception:$e");
      _registerBluetoothStateListener();
      await _tryTurnOnBluetooth();
    }
  }

  void _registerBluetoothStateListener() {
    _adapterStateSubscription?.cancel();
    _adapterStateSubscription = FlutterBluePlus.adapterState.listen((state) {
      centralManagerDidUpdateState?.call(state);
      _centralManagerDidUpdateState(state);
    });
  }

  //MARK: - [New] Auto enable Bluetooth
  Future<void> _tryTurnOnBluetooth() async {
    try {
      final currentState = FlutterBluePlus.adapterStateNow;
      debugPrint("🔵 CurrentBluetoothState: $currentState");

      if (currentState == BluetoothAdapterState.off) {
        debugPrint("🔵 BluetoothAlready关闭,In progressAutoRequest打开...");
        await FlutterBluePlus.turnOn();
      } else if (currentState == BluetoothAdapterState.on) {
        //[fixkey]Androidfirstpermissionsuccess+BluetoothAlreadyenable → proactivetriggerscan
        debugPrint(" BluetoothAlready开启,ProactiveTrigger首Time(s)Scan");
        blueSwitch = true;
        if (automaticScanning) {
          startScan();
        }
        if (autoReconnect) {
          reconnect();
        }
      }
    } catch (e) {
      debugPrint(" Auto打开BluetoothFailed:$e");
    }
  }

  //MARK: - Bluetooth status update (auto scan, auto reconnect)
  void _centralManagerDidUpdateState(BluetoothAdapterState state) {
    switch (state) {
      case BluetoothAdapterState.unknown:
        debugPrint("Bluetooth state unknown");
        break;
      case BluetoothAdapterState.unavailable:
        debugPrint("This device does not support Bluetooth");
        break;
      case BluetoothAdapterState.unauthorized:
        debugPrint("No permission to use Bluetooth, please check settings");
        break;
      case BluetoothAdapterState.turningOn:
        debugPrint("Bluetooth is resetting");
        break;
      case BluetoothAdapterState.on:
        debugPrint(" BluetoothAlready打开,AutoStartScan");
        blueSwitch = true;
        //Bluetoothenable autostartscan
        if (automaticScanning) {
          startScan();
        }
        //autoreconnect
        if (autoReconnect) {
          reconnect();
        }
        break;
      case BluetoothAdapterState.turningOff:
        debugPrint("Bluetooth is turning off");
        break;
      case BluetoothAdapterState.off:
        debugPrint("🔌 BluetoothAlready关闭");
        blueSwitch = false;
        //closeafterautoTryreOpen
        _tryTurnOnBluetooth();
        break;
    }
  }

  //MARK: - Scan related (auto execute)
  void startScan() {
    if (FlutterBluePlus.adapterStateNow != BluetoothAdapterState.on) {
      debugPrint("Bluetooth is not ready when scanning");
      //stateNotMeet / Satisfy,autoOpenBluetooth
      _tryTurnOnBluetooth();
      return;
    }

    discoveredDevices.clear();
    debugPrint("🔍 StartAutoScan附近BLEDevice");

    FlutterBluePlus.startScan(
      withServices: [Guid(targetServiceUUID), Guid(danceTargetServiceUUID)],
      continuousUpdates: true,
      removeIfGone: _deviceTimeout,
    );

    _scanSubscription = FlutterBluePlus.scanResults.listen(
      (results) {
        for (var result in results) {
          _centralManagerDidDiscoverPeripheral(result);
        }
      },
      onError: (e) {
        debugPrint("Scan error: $e");
      },
    );

    _startCleanupTimer();
  }

  //Corresponds to iOS centralManager didDiscover peripheral method
  void _centralManagerDidDiscoverPeripheral(ScanResult result) {
    final advertisementDataMap = {
      ValueConstant.advName: result.advertisementData.advName,
      ValueConstant.txPowerLevel: result.advertisementData.txPowerLevel,
      ValueConstant.connectable: result.advertisementData.connectable,
      ValueConstant.serviceUuids: result.advertisementData.serviceUuids
          .map((g) => g.toString())
          .toList(),
      ValueConstant.serviceData: result.advertisementData.serviceData.map(
        (k, v) => MapEntry(k.toString(), v),
      ),
      ValueConstant.manufacturerData: result.advertisementData.manufacturerData,
    };

    final deviceInfo = BlueDeviceInfo(
      device: result.device,
      advertisementData: advertisementDataMap,
      rssi: result.rssi,
      lastSeen: DateTime.now(),
    );

    final index = discoveredDevices.indexWhere(
      (d) => d.device.remoteId == result.device.remoteId,
    );
    if (index == -1) {
      discoveredDevices.add(deviceInfo);
    } else {
      discoveredDevices[index] = deviceInfo;
    }

    //[Fix]WhenbounddeviceCurrentlyinconnectorconnectedwhen,NotexecutedevicediscoverStreamProcess / Thread
    if (currentPeripheral != null) {
      return;
    }

    //Determine behavior based on blueMode
    switch (blueMode) {
      case 1:
        //Change WiFi mode: Check device changes, auto connect bound devices
        _checkDeviceChanges();
        break;
      case 2:
        //Dance mode: Only connect own device (requires deviceControlMode == 1)
        if (AppState.shared.deviceControlMode == 1) {
          screenMyDevice(discoveredDevices);
        }
        break;
      case 3:
        //pairingmode:callbackdevicelistFor / ToUIshow
        blufDevicesMonitoring?.call(discoveredDevices);
        break;
    }
  }

  ///checkdevicelistchange，hasnewdevicewhencheckwhetherbound
  Future<void> _checkDeviceChanges() async {
    final newDate = DateTime.now();
    if (AppState.shared.manualShutdownTime != null) {
      final Duration difference = newDate.difference(
        AppState.shared.manualShutdownTime!,
      );
      if (difference.inSeconds < 6) {
        return;
      }
    }

    //Get MAC addresses of all current devices
    List<String> currentMacs = [];
    for (final device in discoveredDevices) {
      final mac = _getDeviceId(device);
      if (mac != null) {
        currentMacs.add(mac.toUpperCase());
      }
    }
    //comparewhetherhasnewdeviceAppear / Occur
    bool hasNewDevice = false;
    for (final mac in currentMacs) {
      if (!cachedDeviceMacs.contains(mac)) {
        hasNewDevice = true;
        break;
      }
    }

    //updatecache
    cachedDeviceMacs = currentMacs;
    //ifhasnewdevice,AnduserAlreadylogin,checkwhetherisbounddevice
    if (hasNewDevice && AppState.shared.isLogin.value) {
      //Getbounddevicelist
      await AppState.shared.getDevices();

      //checkcurrentscantodevicewhetherinboundlistin
      for (final deviceInfo in discoveredDevices) {
        final String? deviceMac = _getDeviceId(deviceInfo);
        if (deviceMac == null) continue;

        final upperMac = deviceMac.toUpperCase();
        //checkwhetherisbounddevice
        final isBound = AppState.shared.devices.any(
          (device) => device.mac.toUpperCase() == upperMac,
        );

        if (isBound && currentPeripheral == null) {
          debugPrint(
            "✅ 发现已绑定设备: ${deviceInfo.device.platformName}, MAC: $deviceMac, 自动连接",
          );
          currentPeripheral = deviceInfo.device;
          //[Fix]First / Previouslymarkconnectin,connectsuccessafterAgainpopup
          await connect(deviceInfo.device);
          if (AppState.shared.popupState) {
            return;
          }
          AppState.shared.popupState = true;
          await showCupertinoSheet(
            context: App.appContext(),
            builder: (context) {
              return DeviceWifiConfig();
            },
          );
          AppState.shared.popupState = false;
          break;
        }
      }
    }
  }

  String? _getDeviceId(BlueDeviceInfo blueDeviceInfo) {
    final Map<int, List<int>> manufacturerDataMap =
        blueDeviceInfo.advertisementData[ValueConstant.manufacturerData];
    if (manufacturerDataMap.isNotEmpty) {
      final MapEntry<int, List<int>> firstEntry =
          manufacturerDataMap.entries.first;
      final List<int> customData = firstEntry.value;
      final address = customData.map((byte) {
        return byte.toRadixString(16).padLeft(2, '0').toUpperCase();
      }).join();
      return address;
    }
    return null;
  }

  void screenMyDevice(List<BlueDeviceInfo> devices) {
    if (AppState.shared.deviceMac.isEmpty) {
      return;
    }
    for (final deviceInfo in devices) {
      final String? deviceMac = _getDeviceId(deviceInfo);
      if (deviceMac == null) {
        continue;
      }
      final String targetMac = AppState.shared.deviceMac.toUpperCase();
      if (deviceMac.toUpperCase() == targetMac) {
        debugPrint(
          "✅ 匹配到目标设备: ${deviceInfo.device.platformName}, MAC: $deviceMac",
        );
        currentPeripheral = deviceInfo.device;
        connect(deviceInfo.device);
        break;
      }
    }
  }

  void _startCleanupTimer() {
    _cleanupTimer?.cancel();
    _cleanupTimer = Timer.periodic(const Duration(seconds: 2), (timer) {
      final now = DateTime.now();
      final originalCount = discoveredDevices.length;

      discoveredDevices.removeWhere((d) {
        return now.difference(d.lastSeen) > _deviceTimeout;
      });

      if (discoveredDevices.length != originalCount) {
        blufDevicesMonitoring?.call(discoveredDevices);
      }
    });
  }

  void stopScan() {
    debugPrint("⏹️ StopScan");
    FlutterBluePlus.stopScan();
    _scanSubscription?.cancel();
    _cleanupTimer?.cancel();
  }

  //MARK: - Connection related
  Future<void> connect(BluetoothDevice peripheral) async {
    debugPrint("🔗 StartConnectDevice: ${peripheral.platformName}");
    try {
      _connectionStateSubscription?.cancel();
      _connectionStateSubscription = peripheral.connectionState.listen((state) {
        _handleConnectionState(peripheral, state);
      });
      _resetCharacteristics();
      await peripheral.connect(
        license: License.free,
        timeout: const Duration(seconds: 15),
        autoConnect: false,
      );
    } catch (e) {
      String errorMsg = e is FlutterBluePlusException
          ? "${e.description} (code: ${e.code})"
          : e.toString();
      debugPrint(" ConnectFailed: $errorMsg");
      connectionStateChanged?.call(peripheral, false);
    }
  }

  void _handleConnectionState(
    BluetoothDevice peripheral,
    BluetoothConnectionState state,
  ) {
    switch (state) {
      case BluetoothConnectionState.connected:
        debugPrint(" ConnectSuccess: ${peripheral.platformName}");
        currentPeripheral = peripheral;
        _peripheralDidConnect(peripheral);
        connectionStateChanged?.call(peripheral, true);
        break;
      case BluetoothConnectionState.disconnected:
        final disconnectReason = peripheral.disconnectReason;
        debugPrint(
          "🔌 设备断开: ${peripheral.platformName}, ${disconnectReason?.description ?? "no error"}",
        );
        currentPeripheral = null;
        _resetCharacteristics();
        connectionStateChanged?.call(peripheral, false);

        //Remove disconnected device MAC from cache so it's treated as new when scanned after reboot
        for (final deviceInfo in discoveredDevices) {
          if (deviceInfo.device.remoteId == peripheral.remoteId) {
            final mac = _getDeviceId(deviceInfo);
            if (mac != null) {
              cachedDeviceMacs.remove(mac.toUpperCase());
              debugPrint("🔌 AlreadyWillDisconnectDevice从CacheInRemove: $mac");
            }
            break;
          }
        }

        if (autoReconnect) {
          reconnect();
        }
        break;
      default:
        break;
    }
  }

  Future<void> _peripheralDidConnect(BluetoothDevice peripheral) async {
    await _discoverServices(peripheral);
  }

  Future<void> _discoverServices(BluetoothDevice peripheral) async {
    try {
      if (peripheral.isDisconnected) {
        debugPrint(" DeviceDisconnected,No法DiscoverService");
        return;
      }
      debugPrint("=====================================");
      debugPrint(" StartDiscoverService [Device: ${peripheral.platformName}]");
      debugPrint("=====================================");

      //CallSystemmethoddiscoverservice
      final services = await peripheral.discoverServices(timeout: 35);

      debugPrint(" DiscoverServiceSuccess,TotalCount / Number量: ${services.length}");
      for (var s in services) {
        debugPrint("🟢 ServiceUUID: ${s.uuid}");
      }

      //iteratediscoverfeature
      for (var service in services) {
        await _discoverCharacteristics(peripheral, service);
      }
    } catch (e, stack) {
      //Add stack print trace
      debugPrint("=====================================");
      debugPrint(" DiscoverService[彻底Failed]");
      debugPrint(" ErrorType: ${e.runtimeType}");
      debugPrint(" ErrorInfo: $e");
      debugPrint(" Error堆Stack: $stack");
      debugPrint("=====================================");
    }
  }

  Future<void> _discoverCharacteristics(
    BluetoothDevice peripheral,
    BluetoothService service,
  ) async {
    try {
      final characteristics = service.characteristics;
      debugPrint("🔍 DiscoverService[${service.uuid}]DownCharacteristicCount / Number量: ${characteristics.length}");

      for (var characteristic in characteristics) {
        debugPrint("🔍 DiscoverCharacteristic: ${characteristic.uuid}");
        characteristicCallback?.call(peripheral, characteristic);
        await _setupCharacteristicListener(peripheral, characteristic);
        _saveCharacteristicReference(characteristic);
      }
    } catch (e) {
      debugPrint(" Discover特征Failed: $e");
    }
  }

  Future<void> _setupCharacteristicListener(
    BluetoothDevice peripheral,
    BluetoothCharacteristic characteristic,
  ) async {
    final uuid = characteristic.uuid.toString();

    const List<String> needNotifyUuids = [wifiSetCharacteristicUUID];

    if (!needNotifyUuids.map((e) => e.toLowerCase()).contains(uuid)) {
      debugPrint("ℹ️ Characteristic[$uuid] Not in白名单,SkipListener");
      return;
    }

    //onlyhasinwhitelistInsidefeatureValue,Only thenexecuteDownSurface / Sidelistenlogic
    if (characteristic.properties.notify ||
        characteristic.properties.indicate) {
      try {
        bool notifySuccess = await characteristic.setNotifyValue(true);
        if (notifySuccess) {
          debugPrint(" Characteristic[$uuid] Listener开启Success");
        } else {
          debugPrint(" Characteristic[$uuid] Listener开启Failed");
        }
      } catch (e) {
        debugPrint(" Characteristic[$uuid] ListenerSettingsException: $e");
      }
    }
    //listendatareceive
    characteristic.lastValueStream.listen((value) {
      if (value.isEmpty) return;
      debugPrint("📥 Characteristic[$uuid] 收到Data: ${utf8.decode(value)}");
      if (uuid == wifiSetCharacteristicUUID.toLowerCase()) {
        wifiSetCharacteristicCall?.call(value);
      }
    });
  }

  void _saveCharacteristicReference(BluetoothCharacteristic characteristic) {
    final uuid = characteristic.uuid.toString();
    switch (uuid) {
      case headCharacteristicUUID:
        writeHeadCharacteristic = characteristic;
        break;
      case wifiSetCharacteristicUUID:
        writeWifiSetCharacteristic = characteristic;
        break;
      case expressionCharacteristicUUID:
        writeExpressionCharacteristic = characteristic;
        break;
      case writeCharacteristicUUID:
        writeCharacteristic = characteristic;
        break;
      case motionCharacteristicUUID:
        writeMotionCharacteristic = characteristic;
        break;
      case avatarCharacteristicUUID:
        writeAvatarCharacteristic = characteristic;
        break;
      case rgbCharacteristicUUID:
        writeRGBCharacteristic = characteristic;
        break;
    }
  }

  Future<void> disconnectCurrentPeripheral() async {
    final peripheral = currentPeripheral;
    if (peripheral == null) {
      debugPrint(" NoConnectedDevice");
      return;
    }

    try {
      await peripheral.disconnect(timeout: 35, queue: true, androidDelay: 2000);
      _resetCharacteristics();
    } catch (e) {
      debugPrint("DisconnectFailed: $e");
    }
  }

  void _resetCharacteristics() {
    writeWifiSetCharacteristic = null;
    writeHeadCharacteristic = null;
    writeExpressionCharacteristic = null;
    writeCharacteristic = null;
  }

  //MARK: - Data sending
  Future<void> sendHeadData(String data) async {
    await _sendData(data, writeHeadCharacteristic, "Head data");
  }

  Future<bool> sendWifiSetData(String data) async {
    return await _sendData(data, writeWifiSetCharacteristic, "WiFi set data");
  }

  Future<void> sendExpressionData(String data) async {
    await _sendData(data, writeExpressionCharacteristic, "Expression data");
  }

  Future<void> sendData(String data) async {
    await _sendData(data, writeCharacteristic, "Data");
  }

  Future<void> sendDanceData(DanceData data) async {
    if (writeMotionCharacteristic != null) {
      final motion = MotionData(
        pitchServo: data.pitchServo,
        yawServo: data.yawServo,
      );
      final motionData = utf8.encode(motion.toString());
      writeMotionCharacteristic!.write(
        motionData,
        withoutResponse: false,
        allowLongWrite: true,
      );
    }
    if (writeAvatarCharacteristic != null) {
      final avatar = ExpressionData(
        leftEye: data.leftEye,
        rightEye: data.rightEye,
        mouth: data.mouth,
      );
      final avatarData = utf8.encode(avatar.toString());
      writeAvatarCharacteristic!.write(
        avatarData,
        withoutResponse: false,
        allowLongWrite: true,
      );
    }
    if (writeRGBCharacteristic != null) {
      final rgb = RgbData(
        leftRgbColor: data.leftRgbColor,
        leftRgbDuration: 0.3,
        rightRgbColor: data.rightRgbColor,
        rightRgbDuration: 0.3,
      );
      final rgbData = utf8.encode(rgb.toString());
      writeRGBCharacteristic!.write(
        rgbData,
        withoutResponse: false,
        allowLongWrite: true,
      );
    }
  }

  Future<bool> _sendData(
    String data,
    BluetoothCharacteristic? characteristic,
    String type,
  ) async {
    if (characteristic == null) {
      debugPrint(" 发送Failed:BluetoothDisconnected,No法发送: 发送Within容Is: $data");
      return false;
    }

    final dataToSend = utf8.encode(data);
    if (dataToSend.isEmpty) {
      debugPrint(" 发送Failed:DataIs null/empty");
      return false;
    }

    try {
      debugPrint("📤 In progress发送Data:$data");

      await characteristic.write(
        dataToSend,
        withoutResponse: false,
        allowLongWrite: true,
      );

      debugPrint(" 发送Success!Type:$type");
      return true;
    } catch (e) {
      debugPrint(" 发送Failed!Error:$e");

      //Send failed = connection broken → can reconnect here
      if (e.toString().contains("Timed out")) {
        debugPrint("🔌 Bluetooth发送Timeout,DeviceDisconnected");
        //cantriggerreconnectlogic
      }
      return false;
    }
  }

  Future<void> reconnect() async {
    debugPrint("🔄 Try重连...");
    if (currentPeripheral != null && currentPeripheral!.isDisconnected) {
      _resetCharacteristics();
      await connect(currentPeripheral!);
      onReconnectSuccess?.call(currentPeripheral!);
    }
  }

  Function(BluetoothDevice)? onReconnectSuccess;

  Future<int?> readRssi(BluetoothDevice device) async {
    if (device.isDisconnected) return null;
    try {
      return await device.readRssi(timeout: 15);
    } catch (e) {
      return null;
    }
  }

  //MARK: - Resource release
  void dispose() {
    _scanSubscription?.cancel();
    _adapterStateSubscription?.cancel();
    _connectionStateSubscription?.cancel();
    _cleanupTimer?.cancel();
    stopScan();
    disconnectCurrentPeripheral();
  }
}

extension ScanResultListExtension on List<ScanResult> {
  void addOrUpdate(ScanResult result) {
    final index = indexWhere(
      (element) => element.device.remoteId == result.device.remoteId,
    );
    if (index == -1) {
      add(result);
    } else {
      this[index] = result;
    }
  }
}

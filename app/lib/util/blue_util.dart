/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:flutter/services.dart';
import 'package:stack_chan/native/native_bluetooth_bridge.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/model/expression_data.dart';
import 'package:stack_chan/util/value_constant.dart';
import 'package:stack_chan/view/app.dart';
import 'package:stack_chan/view/popup/device_wifi_config.dart';

import '../model/blue_device_info.dart';
import '../model/dance_list.dart';
import '../view/util/popup.dart';

class BlueSendResult {
  const BlueSendResult._({required this.success, this.errorMessage});

  const BlueSendResult.success() : this._(success: true);

  const BlueSendResult.failure(String message)
    : this._(success: false, errorMessage: message);

  final bool success;
  final String? errorMessage;

  String get displayMessage => errorMessage ?? "Bluetooth send failed.";
}

class BlueUtil {
  static final BlueUtil shared = BlueUtil._internal();

  BlueUtil._internal() {
    _initialize();
  }

  static const String danceTargetServiceUUID =
      "e2e5e5e0-1234-5678-1234-56789abcdef0";

  // MARK: - Core constants (shared with iOS)
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
  static const int _bleFragmentMagic0 = 0xAA;
  static const int _bleFragmentMagic1 = 0x55;
  static const int _bleFragmentMagic2 = 0xC3;
  static const int _bleFragmentVersion = 1;
  static const int _bleFragmentHeaderLength = 10;
  static const int _defaultBlePayload = 20;
  static const int _requestedBleMtu = 247;
  static const int _bleGattOverhead = 3;
  static const int _maxTransferPayloadLength = 2048;

  // MARK: - Core properties (shared with iOS)
  List<BlueDeviceInfo> discoveredDevices = [];
  bool blueSwitch = false;
  final bool autoReconnect = true;
  NativeBluetoothDevice? currentPeripheral;

  // Scanning starts automatically by default.
  bool automaticScanning = true;

  // Write characteristics discovered on the connected device.
  NativeBluetoothCharacteristic? writeCharacteristic;
  NativeBluetoothCharacteristic? writeExpressionCharacteristic;
  NativeBluetoothCharacteristic? writeHeadCharacteristic;
  NativeBluetoothCharacteristic? writeWifiSetCharacteristic;

  // MARK: - Callback closures
  Function(List<BlueDeviceInfo>)? blufDevicesMonitoring;
  Function(NativeBluetoothAdapterState)? centralManagerDidUpdateState;
  FutureOr<void> Function(NativeBluetoothDevice, NativeBluetoothCharacteristic)?
  characteristicCallback;
  Function(NativeBluetoothDevice, bool)? connectionStateChanged;
  Function(List<int>)? wifiSetCharacteristicCall;

  // MARK: - Private properties
  StreamSubscription? _scanSubscription;
  StreamSubscription<int>? _scanErrorSubscription;
  StreamSubscription<NativeBluetoothAdapterState>? _adapterStateSubscription;
  StreamSubscription<NativeBluetoothConnectionState>?
  _connectionStateSubscription;
  final Map<String, StreamSubscription<List<int>>>
  _characteristicSubscriptions = {};
  final Map<String, _BleIncomingPacketAssembler> _blePacketAssemblers = {};
  Timer? _cleanupTimer;
  Timer? _scanRetryTimer;
  Timer? _androidFirstScanRestartTimer;
  final Duration _deviceTimeout = const Duration(seconds: 3);
  DateTime? _lastBluetoothOffPromptTime;
  Future<void>? _permissionRequestFuture;
  Future<void>? _turnOnBluetoothFuture;
  bool _bluetoothStateListenerRegistered = false;
  bool _hasAutoRequestedBluetoothEnable = false;
  bool _scanStartInProgress = false;
  bool _wantsScanning = false;
  bool _androidFirstScanRestartDone = false;

  static const String motionCharacteristicUUID =
      "e2e5e5e1-1234-5678-1234-56789abcdef0";
  static const String avatarCharacteristicUUID =
      "e2e5e5e2-1234-5678-1234-56789abcdef0";
  static const String configCharacteristicUUID =
      "e2e5e5e3-1234-5678-1234-56789abcdef0";
  static const String rgbCharacteristicUUID =
      "e2e5e5e4-1234-5678-1234-56789abcdef0";
  NativeBluetoothCharacteristic? writeMotionCharacteristic;
  NativeBluetoothCharacteristic? writeAvatarCharacteristic;
  NativeBluetoothCharacteristic? writeRGBCharacteristic;
  int? _sendPayloadLimit;
  Future<int>? _sendPayloadNegotiation;
  String? _mtuDeviceId;

  int blueMode = 1; // 1 = Wi-Fi, 2 = Dance, 3 = Pairing.

  String? _activeWifiPairingRemoteId;

  // MARK: - Initialization (Android: permission, listener, Bluetooth)
  void _initialize() {
    // Request permissions before registering listeners and enabling Bluetooth.
    _requestBluetoothPermissions();
  }

  // MARK: - Request Bluetooth permission (permission_handler)
  Future<void> _requestBluetoothPermissions() async {
    final runningRequest = _permissionRequestFuture;
    if (runningRequest != null) {
      return runningRequest;
    }

    final request = _requestBluetoothPermissionsInternal();
    _permissionRequestFuture = request.whenComplete(() {
      _permissionRequestFuture = null;
    });
    return _permissionRequestFuture!;
  }

  Future<void> _requestBluetoothPermissionsInternal() async {
    try {
      Map<Permission, PermissionStatus> statuses;

      if (Platform.isAndroid) {
        // Android 12+ permissions.
        statuses = await [
          Permission.bluetooth,
          Permission.bluetoothScan,
          Permission.bluetoothConnect,
          Permission.location,
        ].request();
      } else if (Platform.isIOS) {
        // iOS permissions.
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

      // Attempt to enable Bluetooth even if permission is denied, working around plugin bugs.
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
    if (_bluetoothStateListenerRegistered) {
      return;
    }
    _bluetoothStateListenerRegistered = true;
    _adapterStateSubscription = NativeBluetoothBridge.shared.adapterState
        .listen((state) {
          centralManagerDidUpdateState?.call(state);
          _centralManagerDidUpdateState(state);
        });
    _scanErrorSubscription = NativeBluetoothBridge.shared.scanErrors.listen((
      code,
    ) {
      debugPrint("Android BLE scan failed, code: $code");
      _scheduleScanRetry(
        delay: const Duration(milliseconds: 900),
        reason: "native scan failed: $code",
      );
    });
  }

  // MARK: - Automatic Bluetooth enablement
  Future<void> _tryTurnOnBluetooth({NativeBluetoothAdapterState? knownState}) {
    final runningAttempt = _turnOnBluetoothFuture;
    if (runningAttempt != null) {
      return runningAttempt;
    }

    late final Future<void> attempt;
    attempt = _tryTurnOnBluetoothInternal(knownState: knownState)
        .whenComplete(() {
          if (identical(_turnOnBluetoothFuture, attempt)) {
            _turnOnBluetoothFuture = null;
          }
        });
    _turnOnBluetoothFuture = attempt;
    return attempt;
  }

  Future<void> _tryTurnOnBluetoothInternal({
    NativeBluetoothAdapterState? knownState,
  }) async {
    try {
      // Adapter state events already contain the authoritative state. Re-querying
      // here used to broadcast the same state again and recursively trigger this
      // callback, flooding Android's platform channel until the engine aborted.
      final currentState =
          knownState ??
          await NativeBluetoothBridge.shared.refreshAdapterState();
      debugPrint("🔵 CurrentBluetoothState: $currentState");

      if (currentState == NativeBluetoothAdapterState.off) {
        debugPrint("🔵 BluetoothAlready关闭,In progressAutoRequest打开...");
        if (Platform.isAndroid) {
          await _requestBluetoothEnableOnce();
          final latestState = await _waitForAdapterOn();
          if (latestState == NativeBluetoothAdapterState.on &&
              automaticScanning) {
            startScan();
          }
        } else if (Platform.isIOS) {
          _showBluetoothOffHintIfNeeded();
          await _requestBluetoothEnableOnce();
        }
      } else if (currentState == NativeBluetoothAdapterState.on) {
        // On Android, start scanning once permission and Bluetooth are ready.
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
      if (Platform.isIOS) {
        _showBluetoothOffHintIfNeeded();
      }
    }
  }

  Future<NativeBluetoothAdapterState> _waitForAdapterOn() async {
    var latestState = await NativeBluetoothBridge.shared.refreshAdapterState();
    for (
      var attempt = 0;
      Platform.isAndroid &&
          latestState != NativeBluetoothAdapterState.on &&
          attempt < 6;
      attempt++
    ) {
      await Future.delayed(const Duration(milliseconds: 500));
      latestState = await NativeBluetoothBridge.shared.refreshAdapterState();
    }
    return latestState;
  }

  Future<bool> ensureBluetoothReady({bool triggerScan = true}) async {
    await _requestBluetoothPermissions();

    final state = await NativeBluetoothBridge.shared.refreshAdapterState();
    if (state == NativeBluetoothAdapterState.on) {
      blueSwitch = true;
      if (triggerScan && automaticScanning) {
        startScan();
      }
      return true;
    }

    await _tryTurnOnBluetooth();
    final latestState = NativeBluetoothBridge.shared.adapterStateNow;
    final isReady = latestState == NativeBluetoothAdapterState.on;
    blueSwitch = isReady;
    if (!isReady) {
      _showBluetoothOffHintIfNeeded();
    }
    return isReady;
  }

  void _showBluetoothOffHintIfNeeded() {
    final now = DateTime.now();
    if (_lastBluetoothOffPromptTime != null &&
        now.difference(_lastBluetoothOffPromptTime!) <
            const Duration(seconds: 3)) {
      return;
    }
    _lastBluetoothOffPromptTime = now;
    AppState.shared.showToast("请先打开蓝牙开关");
  }

  Future<void> _requestBluetoothEnableOnce() async {
    if (_hasAutoRequestedBluetoothEnable) {
      return;
    }
    _hasAutoRequestedBluetoothEnable = true;
    if (Platform.isAndroid) {
      await NativeBluetoothBridge.shared.turnOn();
    } else if (Platform.isIOS) {
      await openAppSettings();
    }
  }

  // MARK: - Bluetooth status update (auto scan, auto reconnect)
  void _centralManagerDidUpdateState(NativeBluetoothAdapterState state) {
    switch (state) {
      case NativeBluetoothAdapterState.unknown:
        debugPrint("Bluetooth state unknown");
        break;
      case NativeBluetoothAdapterState.unavailable:
        debugPrint("This device does not support Bluetooth");
        break;
      case NativeBluetoothAdapterState.unauthorized:
        debugPrint("No permission to use Bluetooth, please check settings");
        break;
      case NativeBluetoothAdapterState.turningOn:
        debugPrint("Bluetooth is resetting");
        break;
      case NativeBluetoothAdapterState.on:
        debugPrint(" BluetoothAlready打开,AutoStartScan");
        blueSwitch = true;
        // Start scanning when Bluetooth becomes available.
        if (automaticScanning) {
          startScan();
        }
        // Reconnect to the last device.
        if (autoReconnect) {
          reconnect();
        }
        break;
      case NativeBluetoothAdapterState.turningOff:
        debugPrint("Bluetooth is turning off");
        break;
      case NativeBluetoothAdapterState.off:
        debugPrint("🔌 BluetoothAlready关闭");
        blueSwitch = false;
        // The event already provides the current state; do not synchronously
        // query and re-emit it from inside its own listener.
        unawaited(_tryTurnOnBluetooth(knownState: state));
        break;
    }
  }

  // MARK: - Scanning
  void startScan() {
    _wantsScanning = true;
    unawaited(_startScan());
  }

  Future<void> _startScan() async {
    if (_scanStartInProgress) {
      return;
    }
    _scanStartInProgress = true;

    try {
      final state = await NativeBluetoothBridge.shared.refreshAdapterState();
      if (state != NativeBluetoothAdapterState.on) {
        blueSwitch = false;
        debugPrint("Bluetooth is not ready when scanning");
        // The adapter is not ready; try to enable Bluetooth automatically.
        await _tryTurnOnBluetooth();
        if (_wantsScanning) {
          _scheduleScanRetry(
            delay: const Duration(milliseconds: 800),
            reason: "adapter not ready",
          );
        }
        return;
      }

      blueSwitch = true;
      discoveredDevices.clear();
      debugPrint("🔍 StartAutoScan附近BLEDevice");

      _scanRetryTimer?.cancel();
      _scanSubscription?.cancel();
      _scanSubscription = NativeBluetoothBridge.shared.scanResults.listen(
        _centralManagerDidDiscoverPeripheral,
        onError: (e) {
          debugPrint("Scan error: $e");
        },
      );

      await NativeBluetoothBridge.shared.stopScan().catchError((e) {
        debugPrint("Stop previous scan failed: $e");
      });

      if (Platform.isAndroid && !_androidFirstScanRestartDone) {
        // Android often needs a short settle time right after runtime Bluetooth
        // permission grant, otherwise the first scanner registration can be empty.
        await Future.delayed(const Duration(milliseconds: 500));
      }

      await NativeBluetoothBridge.shared.startScan(
        withServices: Platform.isAndroid
            ? const <String>[]
            : [targetServiceUUID, danceTargetServiceUUID],
        continuousUpdates: true,
        removeIfGone: _deviceTimeout,
      );

      _startCleanupTimer();
      _scheduleAndroidFirstScanRestart();
    } catch (e) {
      debugPrint("Start scan failed: $e");
      _scheduleScanRetry(
        delay: const Duration(milliseconds: 900),
        reason: "start scan exception",
      );
    } finally {
      _scanStartInProgress = false;
    }
  }

  void _scheduleScanRetry({required Duration delay, required String reason}) {
    if (!_wantsScanning || !automaticScanning) {
      return;
    }
    _scanRetryTimer?.cancel();
    debugPrint("Schedule BLE scan retry: $reason");
    _scanRetryTimer = Timer(delay, () {
      if (_wantsScanning && automaticScanning) {
        unawaited(_startScan());
      }
    });
  }

  void _scheduleAndroidFirstScanRestart() {
    if (!Platform.isAndroid || _androidFirstScanRestartDone) {
      return;
    }
    _androidFirstScanRestartDone = true;
    _androidFirstScanRestartTimer?.cancel();
    _androidFirstScanRestartTimer = Timer(const Duration(seconds: 2), () {
      if (!_wantsScanning ||
          !automaticScanning ||
          currentPeripheral != null ||
          discoveredDevices.isNotEmpty) {
        return;
      }
      debugPrint("Android first BLE scan returned empty, restart scanner once");
      unawaited(_startScan());
    });
  }

  // Mirrors iOS centralManager(_:didDiscover:advertisementData:rssi:).
  void _centralManagerDidDiscoverPeripheral(NativeBluetoothScanResult result) {
    final advertisementData = result.advertisementData;
    final advertisementDataMap = {
      ValueConstant.advName:
          advertisementData[ValueConstant.advName] ?? result.device.advName,
      ValueConstant.txPowerLevel: advertisementData[ValueConstant.txPowerLevel],
      ValueConstant.connectable: advertisementData[ValueConstant.connectable],
      ValueConstant.serviceUuids:
          advertisementData[ValueConstant.serviceUuids] ?? const <String>[],
      ValueConstant.serviceData:
          advertisementData[ValueConstant.serviceData] ??
          const <String, List<int>>{},
      ValueConstant.manufacturerData:
          advertisementData[ValueConstant.manufacturerData] ??
          const <int, List<int>>{},
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

    // Pairing mode (blueMode == 3) still needs scan events to surface unbound
    // devices while another peripheral is connected.
    if (currentPeripheral != null && blueMode != 3) {
      return;
    }

    // Dispatch scan results according to the active mode.
    switch (blueMode) {
      case 1:
        // Wi-Fi mode: detect bound devices and connect automatically.
        _checkDeviceChanges();
        break;
      case 2:
        // Dance mode: connect only the selected device when deviceControlMode is 1.
        if (AppState.shared.deviceControlMode == 1) {
          screenMyDevice(discoveredDevices);
        }
        break;
      case 3:
        // Pairing mode: publish discovered devices to the UI.
        blufDevicesMonitoring?.call(discoveredDevices);
        break;
    }
  }

  /// Connect newly discovered devices that are already bound to the account.
  Future<void> _checkDeviceChanges() async {
    if (_activeWifiPairingRemoteId != null) {
      return;
    }
    if (AppState.shared.popupState) {
      return;
    }

    final newDate = DateTime.now();
    if (AppState.shared.manualShutdownTime != null) {
      final Duration difference = newDate.difference(
        AppState.shared.manualShutdownTime!,
      );
      if (difference.inSeconds < 6) {
        return;
      }
    }

    if (!AppState.shared.isLogin.value) {
      return;
    }

    // Refresh the bound-device list.
    await AppState.shared.getDevices();

    // Check whether the scanned device is in the bound-device list
    for (final deviceInfo in discoveredDevices) {
      final String? deviceMac = _getDeviceId(deviceInfo);
      if (deviceMac == null) {
        continue;
      }

      final upperMac = deviceMac.toUpperCase();
      // Check whether the device is already bound
      final isBound = AppState.shared.devices.any(
        (device) => device.mac.toUpperCase() == upperMac,
      );
      if (!isBound) {
        continue;
      }

      final List<dynamic>? serviceUuids =
          deviceInfo.advertisementData[ValueConstant.serviceUuids]
              as List<dynamic>?;
      final List<String> normalizedServiceUuids = (serviceUuids ?? const [])
          .map((uuid) => uuid.toString().toLowerCase())
          .toList();
      final bool hasWifiTargetService = normalizedServiceUuids.contains(
        targetServiceUUID.toLowerCase(),
      );
      if (!hasWifiTargetService) {
        debugPrint(
          "⏭️ 已绑定设备服务不匹配 WiFi 配网弹窗条件, mac: $deviceMac, services: $normalizedServiceUuids",
        );
        continue;
      }

      _activeWifiPairingRemoteId = deviceInfo.device.remoteId;
      try {
        debugPrint(
          "✅ 发现已绑定设备: ${deviceInfo.device.platformName}, MAC: $deviceMac, 自动连接",
        );
        await connect(deviceInfo.device);
        if (deviceInfo.device.isDisconnected) {
          debugPrint("⚠️ 已绑定设备连接未建立，跳过配网弹窗: ${deviceInfo.device.platformName}");
          return;
        }

        // The mode may have changed while connecting; re-check before showing the Wi-Fi setup sheet.
        if (_activeWifiPairingRemoteId == null || blueMode != 1) {
          return;
        }

        if (AppState.shared.popupState) {
          return;
        }
        AppState.shared.popupState = true;
        await showCustomCupertinoSheet(
          context: App.appContext(),
          scrollableBuilder: (context, scrollController) {
            return const DeviceWifiConfig();
          },
        );
        AppState.shared.popupState = false;
      } finally {
        if (_activeWifiPairingRemoteId == deviceInfo.device.remoteId) {
          _activeWifiPairingRemoteId = null;
        }
      }
      break;
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
    _wantsScanning = false;
    _scanRetryTimer?.cancel();
    _androidFirstScanRestartTimer?.cancel();
    unawaited(NativeBluetoothBridge.shared.stopScan());
    _scanSubscription?.cancel();
    _cleanupTimer?.cancel();
  }

  // MARK: - Connection related
  Future<void> connect(NativeBluetoothDevice peripheral) async {
    debugPrint("🔗 StartConnectDevice: ${peripheral.platformName}");
    try {
      _connectionStateSubscription?.cancel();
      _connectionStateSubscription = peripheral.connectionState.listen((state) {
        _handleConnectionState(peripheral, state);
      });
      _cancelAllCharacteristicSubscriptions();
      _resetCharacteristics();
      await peripheral.connect(timeout: const Duration(seconds: 15));
    } catch (e) {
      debugPrint(" ConnectFailed: $e");
      connectionStateChanged?.call(peripheral, false);
    }
  }

  void _handleConnectionState(
    NativeBluetoothDevice peripheral,
    NativeBluetoothConnectionState state,
  ) {
    switch (state) {
      case NativeBluetoothConnectionState.connected:
        debugPrint(" ConnectSuccess: ${peripheral.platformName}");
        currentPeripheral = peripheral;
        _peripheralDidConnect(peripheral);
        _sendPayloadLimit = null;
        _sendPayloadNegotiation = null;
        _mtuDeviceId = peripheral.remoteId;
        connectionStateChanged?.call(peripheral, true);
        break;
      case NativeBluetoothConnectionState.disconnected:
        debugPrint("🔌 设备断开: ${peripheral.platformName}");
        currentPeripheral = null;
        _sendPayloadLimit = null;
        _sendPayloadNegotiation = null;
        _mtuDeviceId = null;
        if (_activeWifiPairingRemoteId == peripheral.remoteId) {
          _activeWifiPairingRemoteId = null;
        }
        _resetCharacteristics();
        _blePacketAssemblers.clear();
        connectionStateChanged?.call(peripheral, false);

        if (autoReconnect) {
          reconnect();
        }
        break;
    }
  }

  Future<void> _peripheralDidConnect(NativeBluetoothDevice peripheral) async {
    await _discoverServices(peripheral);
  }

  Future<void> _discoverServices(NativeBluetoothDevice peripheral) async {
    try {
      if (peripheral.isDisconnected) {
        debugPrint(" DeviceDisconnected,No法DiscoverService");
        return;
      }
      debugPrint("=====================================");
      debugPrint(" StartDiscoverService [Device: ${peripheral.platformName}]");
      debugPrint("=====================================");

      // Discover services through the native platform API.
      final services = await peripheral.discoverServices(timeout: 35);

      debugPrint(
        " DiscoverServiceSuccess,TotalCount / Number量: ${services.length}",
      );
      for (var s in services) {
        debugPrint("🟢 ServiceUUID: ${s.uuid}");
      }

      // Iterate over the discovered characteristics
      for (var service in services) {
        await _discoverCharacteristics(peripheral, service);
      }
    } catch (e, stack) {
      // Include the stack trace when discovery fails.
      debugPrint("=====================================");
      debugPrint(" DiscoverService[彻底Failed]");
      debugPrint(" ErrorType: ${e.runtimeType}");
      debugPrint(" ErrorInfo: $e");
      debugPrint(" Error堆Stack: $stack");
      debugPrint("=====================================");
    }
  }

  Future<void> _discoverCharacteristics(
    NativeBluetoothDevice peripheral,
    NativeBluetoothService service,
  ) async {
    try {
      final characteristics = service.characteristics;
      debugPrint(
        "🔍 DiscoverService[${service.uuid}]DownCharacteristicCount / Number量: ${characteristics.length}",
      );

      for (var characteristic in characteristics) {
        debugPrint("🔍 DiscoverCharacteristic: ${characteristic.uuid}");
        _saveCharacteristicReference(characteristic);
        await _setupCharacteristicListener(peripheral, characteristic);
        await characteristicCallback?.call(peripheral, characteristic);
      }
    } catch (e) {
      debugPrint(" Discover特征Failed: $e");
    }
  }

  Future<void> _setupCharacteristicListener(
    NativeBluetoothDevice peripheral,
    NativeBluetoothCharacteristic characteristic,
  ) async {
    final uuid = characteristic.uuid.toString().toLowerCase();
    final key = '${peripheral.remoteId}::$uuid';

    const List<String> needNotifyUuids = [wifiSetCharacteristicUUID];

    if (!needNotifyUuids.map((e) => e.toLowerCase()).contains(uuid)) {
      debugPrint("ℹ️ Characteristic[$uuid] Not in白名单,SkipListener");
      return;
    }

    // Subscribe only to characteristics in the notify allowlist.
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
    // Listen for incoming data
    await _characteristicSubscriptions[key]?.cancel();
    _characteristicSubscriptions.remove(key);

    _characteristicSubscriptions[key] = characteristic.lastValueStream.listen((
      value,
    ) {
      if (value.isEmpty) return;
      final assembled = _consumeIncomingPacket(key, value);
      if (assembled == null) {
        return;
      }
      debugPrint(
        "📥 Characteristic[$uuid] 收到Data: ${utf8.decode(assembled, allowMalformed: true)}",
      );
      if (uuid == wifiSetCharacteristicUUID.toLowerCase()) {
        wifiSetCharacteristicCall?.call(assembled);
      }
    });
  }

  Future<void> _cancelAllCharacteristicSubscriptions() async {
    for (final sub in _characteristicSubscriptions.values) {
      await sub.cancel();
    }
    _characteristicSubscriptions.clear();
  }

  void _saveCharacteristicReference(
    NativeBluetoothCharacteristic characteristic,
  ) {
    final uuid = characteristic.uuid.toString().toLowerCase();
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
    // Release active peripheral state immediately to avoid blocking discovery while
    // disconnect completes (common in Wi-Fi config → add-device handover flow).
    currentPeripheral = null;
    _sendPayloadLimit = null;
    _sendPayloadNegotiation = null;
    _mtuDeviceId = null;
    if (_activeWifiPairingRemoteId == peripheral.remoteId) {
      _activeWifiPairingRemoteId = null;
    }

    try {
      await peripheral.disconnect(timeout: 35, queue: true, androidDelay: 2000);
    } catch (e) {
      debugPrint("DisconnectFailed: $e");
    }
    await _cancelAllCharacteristicSubscriptions();
    _resetCharacteristics();
  }

  void _resetCharacteristics() {
    writeWifiSetCharacteristic = null;
    writeHeadCharacteristic = null;
    writeExpressionCharacteristic = null;
    writeCharacteristic = null;
    writeMotionCharacteristic = null;
    writeAvatarCharacteristic = null;
    writeRGBCharacteristic = null;
  }

  // MARK: - Data sending
  Future<void> sendHeadData(String data) async {
    await _sendData(data, writeHeadCharacteristic, "Head data");
  }

  Future<BlueSendResult> sendWifiSetData(String data) async {
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
      await _sendData(
        motion.toString(),
        writeMotionCharacteristic,
        "Motion data",
      );
    }
    if (writeAvatarCharacteristic != null) {
      final avatar = ExpressionData(
        leftEye: data.leftEye,
        rightEye: data.rightEye,
        mouth: data.mouth,
      );
      await _sendData(
        avatar.toString(),
        writeAvatarCharacteristic,
        "Avatar data",
      );
    }
    if (writeRGBCharacteristic != null) {
      final rgb = RgbData(
        leftRgbColor: data.leftRgbColor,
        leftRgbDuration: 0.3,
        rightRgbColor: data.rightRgbColor,
        rightRgbDuration: 0.3,
      );
      await _sendData(rgb.toString(), writeRGBCharacteristic, "RGB data");
    }
  }

  Future<BlueSendResult> _sendData(
    String data,
    NativeBluetoothCharacteristic? characteristic,
    String type,
  ) async {
    if (characteristic == null) {
      final message =
          "$type characteristic is unavailable. Bluetooth may be disconnected or service discovery has not finished.";
      debugPrint(" 发送Failed:$message 发送Within容Is: $data");
      return BlueSendResult.failure(message);
    }

    final dataToSend = utf8.encode(data);
    if (dataToSend.isEmpty) {
      const message = "No data to send.";
      debugPrint(" 发送Failed:$message");
      return const BlueSendResult.failure(message);
    }

    try {
      debugPrint("📤 In progress发送Data:$data");
      final mtuPayload = await _negotiateMtuPayload();
      final maxChunkPayload = _maxChunkPayload(mtuPayload);
      if (mtuPayload <= 0) {
        final message = "Invalid BLE MTU payload: $mtuPayload.";
        debugPrint(" 发送Failed:$message");
        return BlueSendResult.failure(message);
      }

      if (maxChunkPayload <= 0 && dataToSend.length > mtuPayload) {
        final message =
            "BLE MTU payload is too small for fragmented $type transfer.";
        debugPrint(" 发送Failed:$message");
        return BlueSendResult.failure(message);
      }

      if (dataToSend.length <= mtuPayload) {
        await characteristic.write(
          dataToSend,
          withoutResponse: false,
          allowLongWrite: true,
        );
      } else {
        if (dataToSend.length > _maxTransferPayloadLength) {
          final message =
              "$type is too long: ${dataToSend.length} bytes, max $_maxTransferPayloadLength bytes.";
          debugPrint(" 发送Failed:$message");
          return BlueSendResult.failure(message);
        }
        if (dataToSend.length > 0xFFFF) {
          const message =
              "Data is too long for the BLE fragmentation protocol.";
          debugPrint(" 发送Failed:$message");
          return const BlueSendResult.failure(message);
        }
        final packetCount =
            (dataToSend.length + maxChunkPayload - 1) ~/ maxChunkPayload;
        if (packetCount > 0xFFFF) {
          final message =
              "Too many BLE fragments are required for $type: $packetCount.";
          debugPrint(" 发送Failed:$message");
          return BlueSendResult.failure(message);
        }
        debugPrint(
          "📦 分包发送: total=${dataToSend.length}, mtuPayload=$mtuPayload, chunk=$maxChunkPayload, packets=$packetCount",
        );

        for (var index = 0; index < packetCount; index++) {
          final start = index * maxChunkPayload;
          final end = (start + maxChunkPayload).clamp(0, dataToSend.length);
          final chunk = dataToSend.sublist(start, end);
          final packet = _buildFragmentPacket(
            chunk,
            index,
            packetCount,
            dataToSend.length,
          );
          await characteristic.write(
            packet,
            withoutResponse: false,
            allowLongWrite: true,
          );
        }
      }

      debugPrint(" 发送Success!Type:$type");
      return const BlueSendResult.success();
    } catch (e) {
      final message = _formatSendError(e, type);
      debugPrint(" 发送Failed!Error:$message");

      // A timeout usually means the connection dropped.
      if (e.toString().contains("Timed out")) {
        debugPrint("🔌 Bluetooth发送Timeout,DeviceDisconnected");
        // The connection is gone; reconnect if allowed
      }
      return BlueSendResult.failure(message);
    }
  }

  String _formatSendError(Object error, String type) {
    if (error is PlatformException) {
      final nativeMessage = error.message?.trim();

      switch (error.code) {
        case "not_connected":
          return _withNativeDetail(
            "Bluetooth device is not connected",
            nativeMessage,
          );
        case "not_found":
          return _withNativeDetail(
            "$type characteristic was not found",
            nativeMessage,
          );
        case "timeout":
          return _withNativeDetail("$type write timed out", nativeMessage);
        case "write_failed":
          return _withNativeDetail("$type write failed", nativeMessage);
        case "mtu_failed":
          return _withNativeDetail("BLE MTU negotiation failed", nativeMessage);
        case "disposed":
          return _withNativeDetail(
            "Bluetooth bridge was disposed",
            nativeMessage,
          );
      }

      return _withNativeDetail(
        "$type failed with native error ${error.code}",
        nativeMessage,
      );
    }

    final message = error.toString().trim();
    if (message.isEmpty) {
      return "$type failed with an unknown error.";
    }
    return "$type failed: $message";
  }

  String _withNativeDetail(String baseMessage, String? nativeMessage) {
    final message = nativeMessage?.trim();
    if (message == null || message.isEmpty) {
      return "$baseMessage.";
    }

    final normalizedBase = _normalizeErrorMessage(baseMessage);
    final normalizedMessage = _normalizeErrorMessage(message);
    if (normalizedBase.contains(normalizedMessage) ||
        normalizedMessage.contains(normalizedBase)) {
      return "$baseMessage.";
    }
    return "$baseMessage: $message.";
  }

  String _normalizeErrorMessage(String message) {
    return message
        .toLowerCase()
        .replaceAll(RegExp(r"[^\w\s]"), "")
        .replaceAll(RegExp(r"\s+"), " ")
        .trim();
  }

  int _maxChunkPayload(int mtuPayload) {
    final chunkPayload = mtuPayload - _bleFragmentHeaderLength;
    if (chunkPayload <= 0 || mtuPayload <= 0) {
      return 0;
    }
    return chunkPayload;
  }

  Future<int> _negotiateMtuPayload() async {
    final peripheral = currentPeripheral;
    if (peripheral == null) {
      return _defaultBlePayload;
    }

    if (_mtuDeviceId == peripheral.remoteId && _sendPayloadLimit != null) {
      return _sendPayloadLimit!;
    }

    if (_sendPayloadNegotiation != null) {
      return _sendPayloadNegotiation!;
    }

    _sendPayloadLimit = null;
    _mtuDeviceId = peripheral.remoteId;
    final negotiation = _requestNegotiatedMtuPayload(peripheral);
    _sendPayloadNegotiation = negotiation;

    try {
      final payload = await negotiation;
      _sendPayloadLimit = payload;
      return payload;
    } finally {
      if (identical(_sendPayloadNegotiation, negotiation)) {
        _sendPayloadNegotiation = null;
      }
    }
  }

  Future<int> _requestNegotiatedMtuPayload(
    NativeBluetoothDevice peripheral,
  ) async {
    try {
      final negotiatedMtu = await peripheral.requestMtu(_requestedBleMtu);
      final payload = _mtuToPayload(negotiatedMtu);
      _sendPayloadLimit = payload;
      return payload;
    } catch (e) {
      debugPrint("📶 MTU协商失败: $e");
      const fallbackPayload = _defaultBlePayload;
      _sendPayloadLimit = fallbackPayload;
      return fallbackPayload;
    }
  }

  int _mtuToPayload(int mtu) {
    if (mtu <= _bleGattOverhead) {
      return _defaultBlePayload;
    }
    final payload = mtu - _bleGattOverhead;
    if (payload <= 0) {
      return _defaultBlePayload;
    }
    if (payload < _defaultBlePayload) {
      return payload;
    }
    if (payload > _requestedBleMtu) {
      return _requestedBleMtu;
    }
    return payload;
  }

  List<int> _buildFragmentPacket(
    List<int> chunkData,
    int packetIndex,
    int totalPackets,
    int totalLength,
  ) {
    final frame = Uint8List(_bleFragmentHeaderLength + chunkData.length);
    frame[0] = _bleFragmentMagic0;
    frame[1] = _bleFragmentMagic1;
    frame[2] = _bleFragmentMagic2;
    frame[3] = _bleFragmentVersion;
    frame[4] = (packetIndex >> 8) & 0xFF;
    frame[5] = packetIndex & 0xFF;
    frame[6] = (totalPackets >> 8) & 0xFF;
    frame[7] = totalPackets & 0xFF;
    frame[8] = (totalLength >> 8) & 0xFF;
    frame[9] = totalLength & 0xFF;
    for (var i = 0; i < chunkData.length; i++) {
      frame[_bleFragmentHeaderLength + i] = chunkData[i];
    }
    return frame.toList();
  }

  List<int>? _consumeIncomingPacket(String key, List<int> rawData) {
    final assembler = _blePacketAssemblers.putIfAbsent(
      key,
      () => _BleIncomingPacketAssembler(),
    );
    return assembler.consume(rawData);
  }

  Future<void> reconnect() async {
    debugPrint("🔄 Try重连...");
    if (currentPeripheral != null && currentPeripheral!.isDisconnected) {
      _resetCharacteristics();
      await connect(currentPeripheral!);
      onReconnectSuccess?.call(currentPeripheral!);
    }
  }

  Function(NativeBluetoothDevice)? onReconnectSuccess;

  Future<int?> readRssi(NativeBluetoothDevice device) async {
    if (device.isDisconnected) return null;
    try {
      return await device.readRssi(timeout: 15);
    } catch (e) {
      return null;
    }
  }

  // MARK: - Resource release
  void dispose() {
    _scanSubscription?.cancel();
    _scanErrorSubscription?.cancel();
    _adapterStateSubscription?.cancel();
    _connectionStateSubscription?.cancel();
    _blePacketAssemblers.clear();
    _bluetoothStateListenerRegistered = false;
    _cleanupTimer?.cancel();
    _scanRetryTimer?.cancel();
    _androidFirstScanRestartTimer?.cancel();
    stopScan();
    disconnectCurrentPeripheral();
  }
}

class _BleIncomingPacketAssembler {
  static const int _maxPayloadLength = 2048;
  List<int> _buffer = [];
  int _expectedLength = 0;
  int _expectedPackets = 0;
  int _expectedIndex = 0;
  bool _inProgress = false;

  List<int>? consume(List<int> rawData) {
    if (!_isBleFragment(rawData)) {
      _reset();
      return rawData;
    }

    final packetIndex = (rawData[4] << 8) | rawData[5];
    final totalPackets = (rawData[6] << 8) | rawData[7];
    final totalLength = (rawData[8] << 8) | rawData[9];
    const chunkStart = BlueUtil._bleFragmentHeaderLength;
    final chunkData = rawData.sublist(chunkStart);

    if (totalPackets == 0 ||
        totalLength == 0 ||
        totalLength > _maxPayloadLength ||
        packetIndex >= totalPackets) {
      _reset();
      return null;
    }

    if (!_inProgress) {
      if (packetIndex != 0) {
        return null;
      }
      _inProgress = true;
      _expectedLength = totalLength;
      _expectedPackets = totalPackets;
      _expectedIndex = 0;
      _buffer = [];
    } else if (packetIndex == 0) {
      _inProgress = true;
      _expectedLength = totalLength;
      _expectedPackets = totalPackets;
      _expectedIndex = 0;
      _buffer = [];
    } else if (totalLength != _expectedLength ||
        totalPackets != _expectedPackets ||
        packetIndex != _expectedIndex) {
      _reset();
      return null;
    }

    final remainingLength = _expectedLength - _buffer.length;
    if (chunkData.length > remainingLength) {
      _reset();
      return null;
    }
    if (remainingLength > 0 && chunkData.isEmpty) {
      _reset();
      return null;
    }

    _buffer.addAll(chunkData);
    if (_buffer.length != _expectedLength && packetIndex != totalPackets - 1) {
      if (chunkData.isNotEmpty) {
        _expectedIndex++;
      }
      return null;
    }

    _expectedIndex++;
    if (_buffer.length < _expectedLength) {
      _reset();
      return null;
    }

    if (_expectedIndex != totalPackets) {
      _reset();
      return null;
    }

    final completed = _buffer.sublist(0, _expectedLength);
    _reset();
    return completed;
  }

  bool _isBleFragment(List<int> data) {
    if (data.length < BlueUtil._bleFragmentHeaderLength) {
      return false;
    }
    if (data[0] != BlueUtil._bleFragmentMagic0 ||
        data[1] != BlueUtil._bleFragmentMagic1 ||
        data[2] != BlueUtil._bleFragmentMagic2 ||
        data[3] != BlueUtil._bleFragmentVersion) {
      return false;
    }
    return true;
  }

  void _reset() {
    _inProgress = false;
    _expectedLength = 0;
    _expectedPackets = 0;
    _expectedIndex = 0;
    _buffer = [];
  }
}

extension NativeBluetoothScanResultListExtension
    on List<NativeBluetoothScanResult> {
  void addOrUpdate(NativeBluetoothScanResult result) {
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

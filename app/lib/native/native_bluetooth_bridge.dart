/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

enum NativeBluetoothAdapterState {
  unknown,
  unavailable,
  unauthorized,
  turningOn,
  on,
  turningOff,
  off,
}

enum NativeBluetoothConnectionState { connected, disconnected }

class NativeBluetoothCharacteristicProperties {
  final bool read;
  final bool write;
  final bool writeWithoutResponse;
  final bool notify;
  final bool indicate;

  const NativeBluetoothCharacteristicProperties({
    this.read = false,
    this.write = false,
    this.writeWithoutResponse = false,
    this.notify = false,
    this.indicate = false,
  });

  bool get canWrite => write || writeWithoutResponse;

  factory NativeBluetoothCharacteristicProperties.fromMap(
    Map<Object?, Object?> map,
  ) {
    return NativeBluetoothCharacteristicProperties(
      read: map['read'] == true,
      write: map['write'] == true,
      writeWithoutResponse: map['writeWithoutResponse'] == true,
      notify: map['notify'] == true,
      indicate: map['indicate'] == true,
    );
  }
}

class NativeBluetoothDevice {
  final String remoteId;
  final String platformName;
  final String advName;

  const NativeBluetoothDevice({
    required this.remoteId,
    required this.platformName,
    required this.advName,
  });

  String get id => remoteId;

  bool get isDisconnected =>
      NativeBluetoothBridge.shared.connectionStateNow(remoteId) !=
      NativeBluetoothConnectionState.connected;

  Stream<NativeBluetoothConnectionState> get connectionState =>
      NativeBluetoothBridge.shared.connectionState(remoteId);

  Future<void> connect({
    Duration timeout = const Duration(seconds: 15),
    bool autoConnect = false,
  }) {
    return NativeBluetoothBridge.shared.connect(
      remoteId,
      timeout: timeout,
      autoConnect: autoConnect,
    );
  }

  Future<void> disconnect({
    int timeout = 35,
    bool queue = true,
    int androidDelay = 2000,
  }) {
    return NativeBluetoothBridge.shared.disconnect(remoteId);
  }

  Future<List<NativeBluetoothService>> discoverServices({int timeout = 35}) {
    return NativeBluetoothBridge.shared.discoverServices(
      remoteId,
      timeout: Duration(seconds: timeout),
    );
  }

  Future<int> requestMtu(int mtu) {
    return NativeBluetoothBridge.shared.requestMtu(remoteId, mtu);
  }

  Future<int?> readRssi({int timeout = 15}) {
    return NativeBluetoothBridge.shared.readRssi(
      remoteId,
      timeout: Duration(seconds: timeout),
    );
  }

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is NativeBluetoothDevice && other.remoteId == remoteId;

  @override
  int get hashCode => remoteId.hashCode;
}

class NativeBluetoothCharacteristic {
  final String deviceId;
  final String serviceUuid;
  final String uuid;
  final NativeBluetoothCharacteristicProperties properties;

  const NativeBluetoothCharacteristic({
    required this.deviceId,
    required this.serviceUuid,
    required this.uuid,
    required this.properties,
  });

  Future<bool> setNotifyValue(bool enabled) {
    return NativeBluetoothBridge.shared.setNotifyValue(
      deviceId: deviceId,
      serviceUuid: serviceUuid,
      characteristicUuid: uuid,
      enabled: enabled,
    );
  }

  Future<void> write(
    List<int> data, {
    bool withoutResponse = false,
    bool allowLongWrite = false,
  }) {
    return NativeBluetoothBridge.shared.writeCharacteristic(
      deviceId: deviceId,
      serviceUuid: serviceUuid,
      characteristicUuid: uuid,
      data: data,
      withoutResponse: withoutResponse,
    );
  }

  Stream<List<int>> get lastValueStream {
    return NativeBluetoothBridge.shared.characteristicValueStream(
      deviceId: deviceId,
      serviceUuid: serviceUuid,
      characteristicUuid: uuid,
    );
  }
}

class NativeBluetoothService {
  final String uuid;
  final List<NativeBluetoothCharacteristic> characteristics;

  const NativeBluetoothService({
    required this.uuid,
    required this.characteristics,
  });
}

class NativeBluetoothScanResult {
  final NativeBluetoothDevice device;
  final Map<String, dynamic> advertisementData;
  final int rssi;

  const NativeBluetoothScanResult({
    required this.device,
    required this.advertisementData,
    required this.rssi,
  });
}

class NativeBluetoothConnectionEvent {
  final String deviceId;
  final NativeBluetoothConnectionState state;
  final String? reason;

  const NativeBluetoothConnectionEvent({
    required this.deviceId,
    required this.state,
    this.reason,
  });
}

class NativeBluetoothCharacteristicValue {
  final String deviceId;
  final String serviceUuid;
  final String characteristicUuid;
  final List<int> value;

  const NativeBluetoothCharacteristicValue({
    required this.deviceId,
    required this.serviceUuid,
    required this.characteristicUuid,
    required this.value,
  });
}

class NativeBluetoothBridge {
  static final NativeBluetoothBridge shared = NativeBluetoothBridge._internal();

  static const MethodChannel _methodChannel = MethodChannel(
    'com.m5stack.stackchan/bluetooth',
  );
  static const EventChannel _eventChannel = EventChannel(
    'com.m5stack.stackchan/bluetooth_events',
  );

  final StreamController<NativeBluetoothScanResult> _scanController =
      StreamController<NativeBluetoothScanResult>.broadcast();
  final StreamController<int> _scanErrorController =
      StreamController<int>.broadcast();
  final StreamController<NativeBluetoothAdapterState> _adapterStateController =
      StreamController<NativeBluetoothAdapterState>.broadcast();
  final StreamController<NativeBluetoothConnectionEvent> _connectionController =
      StreamController<NativeBluetoothConnectionEvent>.broadcast();
  final StreamController<NativeBluetoothCharacteristicValue>
  _characteristicController =
      StreamController<NativeBluetoothCharacteristicValue>.broadcast();

  final Map<String, NativeBluetoothConnectionState> _connectionStates = {};
  StreamSubscription<dynamic>? _eventSubscription;
  Future<NativeBluetoothAdapterState>? _adapterStateRefreshFuture;

  NativeBluetoothAdapterState adapterStateNow =
      NativeBluetoothAdapterState.unknown;

  NativeBluetoothBridge._internal() {
    _eventSubscription = _eventChannel.receiveBroadcastStream().listen(
      _handleNativeEvent,
      onError: (Object error, StackTrace stackTrace) {
        debugPrint('Native Bluetooth event error: $error');
      },
    );
    unawaited(refreshAdapterState());
  }

  Stream<NativeBluetoothScanResult> get scanResults => _scanController.stream;

  Stream<int> get scanErrors => _scanErrorController.stream;

  Stream<NativeBluetoothAdapterState> get adapterState =>
      _adapterStateController.stream;

  Stream<NativeBluetoothConnectionEvent> get connectionEvents =>
      _connectionController.stream;

  NativeBluetoothConnectionState connectionStateNow(String deviceId) {
    return _connectionStates[deviceId] ??
        NativeBluetoothConnectionState.disconnected;
  }

  Stream<NativeBluetoothConnectionState> connectionState(String deviceId) {
    return _connectionController.stream
        .where((event) => event.deviceId == deviceId)
        .map((event) => event.state);
  }

  Stream<List<int>> characteristicValueStream({
    required String deviceId,
    required String serviceUuid,
    required String characteristicUuid,
  }) {
    final normalizedServiceUuid = serviceUuid.toLowerCase();
    final normalizedCharacteristicUuid = characteristicUuid.toLowerCase();
    return _characteristicController.stream
        .where(
          (event) =>
              event.deviceId == deviceId &&
              event.serviceUuid.toLowerCase() == normalizedServiceUuid &&
              event.characteristicUuid.toLowerCase() ==
                  normalizedCharacteristicUuid,
        )
        .map((event) => event.value);
  }

  Future<NativeBluetoothAdapterState> refreshAdapterState() {
    final runningRefresh = _adapterStateRefreshFuture;
    if (runningRefresh != null) {
      return runningRefresh;
    }

    late final Future<NativeBluetoothAdapterState> refresh;
    refresh = _refreshAdapterState().whenComplete(() {
      if (identical(_adapterStateRefreshFuture, refresh)) {
        _adapterStateRefreshFuture = null;
      }
    });
    _adapterStateRefreshFuture = refresh;
    return refresh;
  }

  Future<NativeBluetoothAdapterState> _refreshAdapterState() async {
    try {
      final state = await _methodChannel.invokeMethod<String>(
        'getAdapterState',
      );
      final parsed = _adapterStateFromString(state);
      _setAdapterState(parsed);
      return parsed;
    } catch (e) {
      debugPrint('Get Bluetooth adapter state failed: $e');
      return adapterStateNow;
    }
  }

  Future<bool> turnOn() async {
    final enabled = await _methodChannel.invokeMethod<bool>('turnOn') ?? false;
    await refreshAdapterState();
    return enabled;
  }

  Future<void> startScan({
    required List<String> withServices,
    bool continuousUpdates = true,
    Duration? removeIfGone,
  }) async {
    await _methodChannel.invokeMethod<void>('startScan', {
      'serviceUuids': withServices,
      'continuousUpdates': continuousUpdates,
      'removeIfGoneMillis': removeIfGone?.inMilliseconds,
    });
  }

  Future<void> stopScan() async {
    await _methodChannel.invokeMethod<void>('stopScan');
  }

  Future<void> connect(
    String deviceId, {
    Duration timeout = const Duration(seconds: 15),
    bool autoConnect = false,
  }) async {
    await _methodChannel.invokeMethod<void>('connect', {
      'deviceId': deviceId,
      'timeoutMillis': timeout.inMilliseconds,
      'autoConnect': autoConnect,
    });
  }

  Future<void> disconnect(String deviceId) async {
    await _methodChannel.invokeMethod<void>('disconnect', {
      'deviceId': deviceId,
    });
  }

  Future<List<NativeBluetoothService>> discoverServices(
    String deviceId, {
    Duration timeout = const Duration(seconds: 35),
  }) async {
    final response = await _methodChannel.invokeMethod<List<dynamic>>(
      'discoverServices',
      {'deviceId': deviceId, 'timeoutMillis': timeout.inMilliseconds},
    );
    final services = response ?? const <dynamic>[];
    return services.map((service) {
      final serviceMap = _asMap(service);
      final serviceUuid = serviceMap['uuid']?.toString() ?? '';
      final characteristicsRaw =
          serviceMap['characteristics'] as List<dynamic>? ?? const <dynamic>[];
      final characteristics = characteristicsRaw.map((characteristic) {
        final characteristicMap = _asMap(characteristic);
        return NativeBluetoothCharacteristic(
          deviceId: deviceId,
          serviceUuid: serviceUuid,
          uuid: characteristicMap['uuid']?.toString() ?? '',
          properties: NativeBluetoothCharacteristicProperties.fromMap(
            _asMap(characteristicMap['properties']),
          ),
        );
      }).toList();
      return NativeBluetoothService(
        uuid: serviceUuid,
        characteristics: characteristics,
      );
    }).toList();
  }

  Future<bool> setNotifyValue({
    required String deviceId,
    required String serviceUuid,
    required String characteristicUuid,
    required bool enabled,
  }) async {
    return await _methodChannel.invokeMethod<bool>('setNotifyValue', {
          'deviceId': deviceId,
          'serviceUuid': serviceUuid,
          'characteristicUuid': characteristicUuid,
          'enabled': enabled,
        }) ??
        false;
  }

  Future<void> writeCharacteristic({
    required String deviceId,
    required String serviceUuid,
    required String characteristicUuid,
    required List<int> data,
    required bool withoutResponse,
  }) async {
    await _methodChannel.invokeMethod<void>('writeCharacteristic', {
      'deviceId': deviceId,
      'serviceUuid': serviceUuid,
      'characteristicUuid': characteristicUuid,
      'value': Uint8List.fromList(data),
      'withoutResponse': withoutResponse,
    });
  }

  Future<int> requestMtu(String deviceId, int mtu) async {
    return await _methodChannel.invokeMethod<int>('requestMtu', {
          'deviceId': deviceId,
          'mtu': mtu,
        }) ??
        mtu;
  }

  Future<int?> readRssi(
    String deviceId, {
    Duration timeout = const Duration(seconds: 15),
  }) async {
    return _methodChannel.invokeMethod<int>('readRssi', {
      'deviceId': deviceId,
      'timeoutMillis': timeout.inMilliseconds,
    });
  }

  void dispose() {
    _eventSubscription?.cancel();
    _scanController.close();
    _scanErrorController.close();
    _adapterStateController.close();
    _connectionController.close();
    _characteristicController.close();
  }

  void _handleNativeEvent(dynamic rawEvent) {
    final event = _asMap(rawEvent);
    final type = event['type']?.toString();
    switch (type) {
      case 'adapterStateChanged':
        _setAdapterState(_adapterStateFromString(event['state']?.toString()));
        break;
      case 'scanResult':
        _scanController.add(_scanResultFromMap(_asMap(event['result'])));
        break;
      case 'scanFailed':
        _scanErrorController.add((event['code'] as num?)?.toInt() ?? -1);
        break;
      case 'connectionStateChanged':
        final deviceId = event['deviceId']?.toString();
        if (deviceId == null || deviceId.isEmpty) return;
        final state = _connectionStateFromString(event['state']?.toString());
        _connectionStates[deviceId] = state;
        _connectionController.add(
          NativeBluetoothConnectionEvent(
            deviceId: deviceId,
            state: state,
            reason: event['reason']?.toString(),
          ),
        );
        break;
      case 'characteristicChanged':
        final deviceId = event['deviceId']?.toString();
        final serviceUuid = event['serviceUuid']?.toString();
        final characteristicUuid = event['characteristicUuid']?.toString();
        if (deviceId == null ||
            serviceUuid == null ||
            characteristicUuid == null) {
          return;
        }
        _characteristicController.add(
          NativeBluetoothCharacteristicValue(
            deviceId: deviceId,
            serviceUuid: serviceUuid,
            characteristicUuid: characteristicUuid,
            value: _intList(event['value']),
          ),
        );
        break;
      default:
        debugPrint('Unknown native Bluetooth event: $event');
    }
  }

  void _setAdapterState(NativeBluetoothAdapterState state) {
    if (adapterStateNow == state) {
      return;
    }
    adapterStateNow = state;
    _adapterStateController.add(state);
  }

  NativeBluetoothScanResult _scanResultFromMap(Map<Object?, Object?> map) {
    final deviceMap = _asMap(map['device']);
    final device = NativeBluetoothDevice(
      remoteId: deviceMap['id']?.toString() ?? '',
      platformName: deviceMap['name']?.toString() ?? '',
      advName:
          deviceMap['advName']?.toString() ??
          deviceMap['name']?.toString() ??
          '',
    );
    return NativeBluetoothScanResult(
      device: device,
      advertisementData: _advertisementDataFromMap(
        _asMap(map['advertisementData']),
      ),
      rssi: (map['rssi'] as num?)?.toInt() ?? 0,
    );
  }

  static Map<String, dynamic> _advertisementDataFromMap(
    Map<Object?, Object?> map,
  ) {
    return {
      'advName': map['advName']?.toString() ?? '',
      'txPowerLevel': (map['txPowerLevel'] as num?)?.toInt(),
      'connectable': map['connectable'] == true,
      'serviceUuids': _stringList(map['serviceUuids']),
      'serviceData': _stringByteMap(map['serviceData']),
      'manufacturerData': _manufacturerDataMap(map['manufacturerData']),
    };
  }

  static NativeBluetoothAdapterState _adapterStateFromString(String? value) {
    return NativeBluetoothAdapterState.values.firstWhere(
      (state) => state.name == value,
      orElse: () => NativeBluetoothAdapterState.unknown,
    );
  }

  static NativeBluetoothConnectionState _connectionStateFromString(
    String? value,
  ) {
    return NativeBluetoothConnectionState.values.firstWhere(
      (state) => state.name == value,
      orElse: () => NativeBluetoothConnectionState.disconnected,
    );
  }

  static Map<Object?, Object?> _asMap(dynamic value) {
    if (value is Map) {
      return value.cast<Object?, Object?>();
    }
    return <Object?, Object?>{};
  }

  static List<String> _stringList(dynamic value) {
    if (value is List) {
      return value.map((item) => item.toString()).toList();
    }
    return const <String>[];
  }

  static Map<String, List<int>> _stringByteMap(dynamic value) {
    final result = <String, List<int>>{};
    if (value is Map) {
      value.forEach((key, rawValue) {
        if (key != null) {
          result[key.toString()] = _intList(rawValue);
        }
      });
    }
    return result;
  }

  static Map<int, List<int>> _manufacturerDataMap(dynamic value) {
    final result = <int, List<int>>{};
    if (value is Map) {
      value.forEach((key, rawValue) {
        final companyId = key is int ? key : int.tryParse(key.toString());
        if (companyId != null) {
          result[companyId] = _intList(rawValue);
        }
      });
    }
    return result;
  }

  static List<int> _intList(dynamic value) {
    if (value is Uint8List) {
      return value.toList();
    }
    if (value is List<int>) {
      return value;
    }
    if (value is List) {
      return value.whereType<num>().map((item) => item.toInt()).toList();
    }
    return const <int>[];
  }
}

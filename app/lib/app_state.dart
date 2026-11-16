/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:typed_data';
import 'dart:async';

import 'package:dio/dio.dart' show Response;
import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:flutter/services.dart';
import 'package:geolocator/geolocator.dart';
import 'package:get/get.dart' hide Response;
import 'package:package_info_plus/package_info_plus.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:stack_chan/model/device.dart';
import 'package:stack_chan/model/model.dart';
import 'package:stack_chan/network/http.dart';
import 'package:stack_chan/util/blue_util.dart';
import 'package:stack_chan/util/mac_address_validator.dart';
import 'package:stack_chan/util/value_constant.dart';
import 'package:stack_chan/view/popup/binding_device.dart';
import 'package:stack_chan/view/popup/login_page.dart';
import 'package:stack_chan/view/util/popup.dart';
import 'package:uuid/uuid.dart';

import 'model/blue_device_info.dart';
import 'model/msg_type.dart';
import 'model/user_info.dart';
import 'network/urls.dart';
import 'network/lan_endpoint_announcer.dart';
import 'network/local_area_network_service.dart';
import 'network/device_message_transport.dart';
import 'network/web_socket_util.dart';

class AppState extends GetxController with WidgetsBindingObserver {
  static final AppState shared = Get.find<AppState>();

  static final asyncPrefs = SharedPreferencesAsync();

  bool isInitialization = false;

  PackageInfo? packageInfo;

  AppState() {
    _localConnectionSubscription = LocalAreaNetworkService
        .instance
        .connectionStates
        .listen((_) {
          _updateConnectionMode();
          _endpointAnnouncer.setLanConnected(_isLocalTargetConnected);
        });
    _localEndpointSubscription = LocalAreaNetworkService
        .instance
        .serverEndpoints
        .listen(_endpointAnnouncer.updateEndpoint);
    // Re-announce the endpoint on remote reconnect instead of waiting for the next cycle.
    WebSocketUtil.shared.addConnectionObserver(
      this,
      _endpointAnnouncer.announce,
    );
  }

  StreamSubscription<bool>? _localConnectionSubscription;
  StreamSubscription<LanServerEndpoint?>? _localEndpointSubscription;

  late final LanEndpointAnnouncer _endpointAnnouncer = LanEndpointAnnouncer(
    send: _sendLanEndpoint,
  );

  bool get _isLocalTargetConnected =>
      LocalAreaNetworkService.instance.canSendTo(deviceMac);

  /// Sends the endpoint over the remote channel; the announcer repeats until the firmware connects directly.
  void _sendLanEndpoint(LanServerEndpoint endpoint) {
    if (deviceMac.isEmpty || !WebSocketUtil.shared.isConnected) return;
    sendWebSocketMessage(MsgType.appLanAddress, data: endpoint.encode());
  }

  /// Whether commands for the currently selected device are using LAN.
  final RxBool isLocalConnection = false.obs;

  void _updateConnectionMode() {
    isLocalConnection.value = LocalAreaNetworkService.instance.canSendTo(
      deviceMac,
    );
  }

  late final _deviceMessages = DeviceMessageTransport(
    local: LocalAreaNetworkService.instance,
    remote: sendWebSocketMessage,
  );

  Future<void> initData() async {
    WidgetsBinding.instance.addObserver(this);
    deviceMac = await asyncPrefs.getString(ValueConstant.deviceMac) ?? "";
    _deviceId.value =
        await asyncPrefs.getString(ValueConstant.deviceId) ?? uuid.v4();
    _deviceControlMode.value =
        await asyncPrefs.getInt(ValueConstant.deviceControlMode) ?? 0;
    isInitialization = true;
    isLogin.value = await asyncPrefs.getBool(ValueConstant.isLogin) ?? false;

    /// Make the status bar and navigation bar transparent.
    const style = SystemUiOverlayStyle(
      statusBarColor: CupertinoColors.transparent,
      systemNavigationBarColor: CupertinoColors.transparent,
      systemNavigationBarDividerColor: CupertinoColors.transparent,
      systemStatusBarContrastEnforced: false,
      systemNavigationBarContrastEnforced: false,
    );
    await SystemChrome.setEnabledSystemUIMode(
      SystemUiMode.edgeToEdge,
      overlays: [SystemUiOverlay.top, SystemUiOverlay.bottom],
    );
    SystemChrome.setSystemUIOverlayStyle(style);
    await SystemChrome.setPreferredOrientations([
      .portraitUp,
      .portraitDown,
      .landscapeLeft,
      .landscapeRight,
    ]);

    /// Initialize package metadata.
    packageInfo = await PackageInfo.fromPlatform();
  }

  final uuid = const Uuid();

  final RxString _deviceId = "".obs;

  String get deviceId => _deviceId.value;

  final RxBool isLogin = RxBool(false);

  Future<void> setIsLogin(bool login) async {
    await asyncPrefs.setBool(ValueConstant.isLogin, login);
    isLogin.value = login;
  }

  final RxString _deviceMac = "".obs;

  String get deviceMac => _deviceMac.value;

  set deviceMac(String mac) {
    _deviceMac.value = mac;
    asyncPrefs.setString(ValueConstant.deviceMac, _deviceMac.value);
    startSearchXiaoZhiConfig(mac);
    unawaited(LocalAreaNetworkService.instance.setTargetDevice(mac));
    _updateConnectionMode();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    // The inactive state covers permission prompts, so keep the service; paused/detached releases it, and resume restores it.
    if (state == AppLifecycleState.resumed) {
      unawaited(LocalAreaNetworkService.instance.setForeground(true));
    } else if (state == AppLifecycleState.paused ||
        state == AppLifecycleState.detached) {
      unawaited(LocalAreaNetworkService.instance.setForeground(false));
    }
  }

  @override
  void onClose() {
    WidgetsBinding.instance.removeObserver(this);
    unawaited(LocalAreaNetworkService.instance.dispose());
    unawaited(_localConnectionSubscription?.cancel());
    unawaited(_localEndpointSubscription?.cancel());
    _endpointAnnouncer.dispose();
    WebSocketUtil.shared.removeConnectionObserver(this);
    super.onClose();
  }

  bool get hasValidDeviceMac => _deviceMac.isNotEmpty;

  final RxBool _deviceIsOnline = false.obs;

  bool get deviceIsOnline => _deviceIsOnline.value;

  set deviceIsOnline(bool deviceIsOnline) {
    _deviceIsOnline.value = deviceIsOnline;
  }

  RxList<BlueDeviceInfo> blueDeviceList = RxList([]);

  DateTime? manualShutdownTime;

  bool popupState = false; // Whether a popup is currently shown.

  Rxn<Device> deviceInfo = Rxn();

  final RxInt _deviceControlMode = RxInt(1);

  int get deviceControlMode => _deviceControlMode.value;

  set deviceControlMode(int deviceControlMode) {
    _deviceControlMode.value = deviceControlMode;
    asyncPrefs.setInt(ValueConstant.deviceControlMode, deviceControlMode);
  }

  Function(String?)? toastFunction;

  void showToast(String? msg) {
    if (toastFunction != null) {
      toastFunction!(msg);
    }
  }

  Rxn<UserInfo> userInfo = Rxn();

  /// The phone's current location.
  final Rxn<Position> currentLocation = Rxn<Position>();
  final RxBool isLocationAvailable = false.obs;

  bool showBlueDevicesSetStep = false;

  Future<void> logout() async {
    await setIsLogin(false);
    await asyncPrefs.remove(ValueConstant.token);
    userInfo.value = null;
    deviceInfo.value = null;
    deviceMac = "";
    devices.value = [];
  }

  Future<void> getUserInfo() async {
    final response = await Http.instance.get(Urls.user);
    if (response.data != null) {
      Model<UserInfo> responseData = Model.fromJsonT(
        response.data,
        factory: (value) => UserInfo.fromJson(value),
      );
      if (responseData.isSuccess()) {
        userInfo.value = responseData.data;
      }
    }
  }

  (MsgType?, Uint8List?) parseMessage(Uint8List message) {
    if (message.length < 5) {
      return (null, null);
    }

    final int typeByte = message[0] & 0xFF;
    MsgType? msgType;
    try {
      msgType = MsgType.values.firstWhere((e) => e.value == typeByte);
    } on StateError {
      msgType = null;
    }
    if (msgType == null) {
      return (null, null);
    }

    int dataLength = 0;
    for (int i = 1; i <= 4; i++) {
      dataLength = (dataLength << 8) | (message[i] & 0xFF);
    }

    final int totalRequiredLength = 5 + dataLength;
    if (message.length < totalRequiredLength || dataLength < 0) {
      return (null, null);
    }

    final Uint8List payload = Uint8List.sublistView(
      message,
      5,
      totalRequiredLength,
    );
    return (msgType, payload);
  }

  void sendWebSocketMessage(MsgType msgType, {Uint8List? data}) {
    final payload = data ?? Uint8List(0);
    final buffer = BytesBuilder();

    buffer.add([msgType.value & 0xFF]);

    final int dataLen = payload.length;
    final Uint32List lenBytes = Uint32List(1);
    lenBytes[0] = dataLen;
    buffer.add([
      (lenBytes[0] >> 24) & 0xFF,
      (lenBytes[0] >> 16) & 0xFF,
      (lenBytes[0] >> 8) & 0xFF,
      lenBytes[0] & 0xFF,
    ]);

    buffer.add(payload);
    WebSocketUtil.shared.send(buffer.toBytes());
  }

  /// Routes Avatar/Camera messages over LAN or remote, falling back to the cloud when the LAN is
  /// unavailable, the target differs, or sending fails. Both channels keep the target MAC prefix.
  void sendDeviceMessage(
    MsgType type, {
    required String deviceMac,
    Uint8List? data,
  }) {
    _deviceMessages.send(type, deviceMac: deviceMac, data: data);
  }

  void webSocketMessageMonitoring() {
    WebSocketUtil.shared.addObserver("App", (message) {
      if (message is Uint8List) {
        final result = parseMessage(message);
        final msgType = result.$1;
        if (msgType != null) {
          switch (msgType) {
            case .deviceOnline:
              deviceIsOnline = true;
              // The firmware just entered AVATAR remotely; send it the endpoint immediately.
              _endpointAnnouncer.announce();
              break;
            case .deviceOffline:
              deviceIsOnline = false;
              break;
            default:
              break;
          }
        }
      } else if (message is String) {
        debugPrint("Received a regular message: $message");
      }
    });
  }

  void connectWebSocket() {
    final webSocketUrl =
        "${Urls.getWebSocketUrl()}?deviceType=App&deviceId=${AppState.shared.deviceId}";
    WebSocketUtil.shared.connect(webSocketUrl);
  }

  /// Filters scan results down to devices that support pairing.
  List<BlueDeviceInfo> screeningDevices(List<BlueDeviceInfo> devices) {
    List<BlueDeviceInfo> newDevices = [];
    final allowedServiceUuids = {
      BlueUtil.targetServiceUUID.toUpperCase(),
      BlueUtil.danceTargetServiceUUID.toUpperCase(),
    };

    for (final deviceInfo in devices) {
      final List<dynamic>? serviceUuids =
          deviceInfo.advertisementData[ValueConstant.serviceUuids];
      if (serviceUuids == null || serviceUuids.isEmpty) {
        continue;
      }
      final bool containsTargetUUID = serviceUuids
          .map((uuid) => uuid.toString().toUpperCase())
          .any((uuid) => allowedServiceUuids.contains(uuid));
      if (containsTargetUUID) {
        newDevices.add(deviceInfo);
      }
    }
    return newDevices;
  }

  /// Updates the device information on the server.
  Future<void> updateDeviceInfo() async {
    final Map<String, dynamic> map = {
      ValueConstant.mac: deviceMac,
      ValueConstant.name: deviceInfo.value?.name,
    };
    Response response = await Http.instance.put(Urls.deviceInfo, data: map);
    if (response.data != null) {
      Model<String> data = Model.fromJsonT(response.data);
      if (data.isSuccess()) {
        showToast("Update successful");
        deviceInfo.refresh();
        getDevices(force: true);
      } else {
        showToast("Failed to parse data");
      }
    }
  }

  Future<void> getDeviceInfo() async {
    final Map<String, dynamic> map = {ValueConstant.mac: deviceMac};
    Response response = await Http.instance.get(Urls.deviceInfo, data: map);
    if (response.data != null) {
      Model<Device> model = Model.fromJsonT(
        response.data,
        factory: (value) => Device.fromJson(value),
      );
      if (model.isSuccess() && model.data != null) {
        deviceInfo.value = model.data!;
      }
    }
  }

  // Show the binding sheet.
  void showBindingDevice(BuildContext context) async {
    if (AppState.shared.popupState) {
      return;
    }
    if (AppState.shared.isLogin.value) {
      BlueUtil.shared.blueMode = 3;
      AppState.shared.popupState = true;
      await showCustomCupertinoSheet(
        useNestedNavigation: true,
        context: context,
        enableDrag: false,
        scrollableBuilder: (context, scrollController) {
          return const BindingDevice();
        },
      );
      BlueUtil.shared.blueMode = 1;
      AppState.shared.popupState = false;
      AppState.shared.showBlueDevicesSetStep = false;
    } else {
      await showLoginPopup(context);
    }
  }

  Future<void> showLoginPopup(BuildContext context) async {
    AppState.shared.popupState = true;
    await showCustomCupertinoSheet(
      context: context,
      useNestedNavigation: true,
      enableDrag: false,
      scrollableBuilder: (context, scrollController) {
        return const LoginPage();
      },
    );
    AppState.shared.popupState = false;
  }

  // Query the XiaoZhi configuration status.
  Future<void> startSearchXiaoZhiConfig(String mac) async {}

  RxList<Device> devices = RxList([]);

  /// Limit StackChan device-list request frequency to avoid rapid polling while
  /// BLE scanning is continuously updating and emitting frequent callbacks.
  ///
  /// `v2/devices` is usually only needed to track binding state changes, so
  /// refreshing every 10 seconds is sufficient under normal usage.
  static const Duration _devicesCacheDuration = Duration(seconds: 10);
  Future<void>? _getDevicesFuture;
  DateTime? _lastGetDevicesRequestAt;

  // Get the list of bound devices.
  Future<void> getDevices({bool force = false}) {
    final runningRequest = _getDevicesFuture;
    if (runningRequest != null) {
      return runningRequest;
    }

    final now = DateTime.now();
    final lastRequestAt = _lastGetDevicesRequestAt;
    if (!force &&
        lastRequestAt != null &&
        now.difference(lastRequestAt) < _devicesCacheDuration) {
      return Future.value();
    }

    _lastGetDevicesRequestAt = now;
    late final Future<void> request;
    request = _getDevicesInternal().whenComplete(() {
      if (identical(_getDevicesFuture, request)) {
        _getDevicesFuture = null;
      }
    });
    _getDevicesFuture = request;
    return request;
  }

  Future<void> _getDevicesInternal() async {
    final response = await Http.instance.get(Urls.devices);
    if (response.data != null) {
      Model<List<Device>> responseData = Model.fromJsonT(
        response.data,
        factory: (value) => Device.fromListJson(value),
      );
      if (responseData.isSuccess() && responseData.data != null) {
        devices.value = responseData.data!;
        if (devices.isEmpty) {
          deviceMac = "";
          deviceInfo.value = null;
        } else {
          if (deviceMac.isEmpty) {
            switchDevice(devices.first);
          } else {
            for (final device in devices) {
              if (device.mac == deviceMac) {
                deviceInfo.value = device;
                break;
              }
            }
          }
        }
      }
    }
  }

  void switchDevice(Device device) async {
    deviceInfo.value = device;
    deviceMac = device.mac;
    connectWebSocket();
  }

  Future<bool> bindDevice(String mac) async {
    final normalizedMac = MacAddressValidator.toPureMac(mac) ?? mac;
    final map = {ValueConstant.mac: normalizedMac};
    final response = await Http.instance.post(
      Urls.v2deviceActivateBind,
      data: map,
    );
    if (response.data != null) {
      Model<bool> responseData = Model.fromJsonT(response.data);
      if (responseData.isSuccess()) {
        deviceMac = normalizedMac;
        await getDevices(force: true);
        await getDeviceInfo();
        connectWebSocket();
        return true;
      } else if (responseData.message != null) {
        showToast(responseData.message);
      }
    }
    return false;
  }

  Future<void> unbindDevice(String mac) async {
    final map = {ValueConstant.mac: mac};
    final response = await Http.instance.post(Urls.v2deviceUnbind, data: map);
    if (response.data != null) {
      Model<bool> responseData = Model.fromJsonT(response.data);
      if (responseData.isSuccess()) {
        AppState.shared.showToast("Device unbinding successful");
        AppState.shared.deviceMac = "";
        AppState.shared.deviceInfo.value = null;
        getDevices(force: true);
      } else {
        AppState.shared.showToast(responseData.message);
      }
    }
  }

  /// Gets the phone's current location.
  Future<void> obtainLocation() async {
    try {
      // Check whether location services are enabled.
      bool serviceEnabled = await Geolocator.isLocationServiceEnabled();
      if (!serviceEnabled) {
        showToast(
          "Location service is not enabled. Please enable the location service first",
        );
        isLocationAvailable.value = false;
        return;
      }

      // Check the location permission.
      LocationPermission permission = await Geolocator.checkPermission();
      if (permission == LocationPermission.denied) {
        permission = await Geolocator.requestPermission();
        if (permission == LocationPermission.denied) {
          showToast(
            "The location permission was denied, and thus the location information could not be obtained",
          );
          isLocationAvailable.value = false;
          // Send the user to settings to grant the permission.
          await openAppSettings();
          return;
        }
      }

      if (permission == LocationPermission.deniedForever) {
        showToast(
          "The location permission has been permanently denied. Please enable the location permission in the settings",
        );
        isLocationAvailable.value = false;
        await openAppSettings();
        return;
      }

      // Get the current position.
      Position position = await Geolocator.getCurrentPosition(
        locationSettings: const LocationSettings(
          accuracy: LocationAccuracy.high,
          distanceFilter: 100,
        ),
      );

      currentLocation.value = position;
      isLocationAvailable.value = true;
      debugPrint(
        "GetPositionSuccess: 纬度${position.latitude}, 经度${position.longitude}",
      );
    } catch (e) {
      showToast("Failed to obtain location: ${e.toString()}");
      isLocationAvailable.value = false;
      debugPrint("GetPositionError: $e");
    }
  }

  /// Continuously listens for location changes.
  void startLocationUpdates() {
    Geolocator.getPositionStream(
      locationSettings: const LocationSettings(
        accuracy: LocationAccuracy.high,
        distanceFilter: 100,
      ),
    ).listen(
      (Position position) {
        currentLocation.value = position;
        isLocationAvailable.value = true;
      },
      onError: (e) {
        debugPrint("PositionUpdateError: $e");
        isLocationAvailable.value = false;
      },
    );
  }

  /// Prompts the user to open the Avatar page on StackChan.
  void showSwitchDeviceAvatar(BuildContext context) {
    showCupertinoDialog(
      context: context,
      builder: (context) => CupertinoAlertDialog(
        title: const Text(
          "Please switch to StackChan and enter Avatar to use this function.",
        ),
        content: Padding(
          padding: const .only(top: 10),
          child: SizedBox(
            height: 180,
            child: Image.asset("assets/stackchan_avatar_page.png"),
          ),
        ),
        actions: [
          CupertinoDialogAction(
            child: const Text("I have understood."),
            onPressed: () => Navigator.maybePop(context),
          ),
        ],
      ),
    );
  }

  /// Prompts the user to open the Dance page on StackChan.
  void showSwitchDeviceDance(BuildContext context) {
    showCupertinoDialog(
      context: context,
      builder: (context) => CupertinoAlertDialog(
        title: const Text(
          "Please switch to StackChan and enter Dance to use this function.",
        ),
        content: Padding(
          padding: const .only(top: 10),
          child: SizedBox(
            height: 180,
            child: Image.asset("assets/stackchan_dance_page.png"),
          ),
        ),
        actions: [
          CupertinoDialogAction(
            child: const Text("I have understood."),
            onPressed: () => Navigator.maybePop(context),
          ),
        ],
      ),
    );
  }
}

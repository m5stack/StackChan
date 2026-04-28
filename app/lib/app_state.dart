/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:typed_data';

import 'package:dio/dio.dart' show Response;
import 'package:flutter/cupertino.dart';
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
import 'package:stack_chan/util/value_constant.dart';
import 'package:stack_chan/view/popup/binding_device.dart';
import 'package:stack_chan/view/popup/login_page.dart';
import 'package:uuid/uuid.dart';

import 'model/blue_device_info.dart';
import 'model/msg_type.dart';
import 'model/user_info.dart';
import 'network/urls.dart';
import 'network/web_socket_util.dart';

class AppState extends GetxController {
  static final AppState shared = Get.find<AppState>();

  static final asyncPrefs = SharedPreferencesAsync();

  bool isInitialization = false;

  PackageInfo? packageInfo;

  AppState();

  Future<void> initData() async {
    deviceMac = await asyncPrefs.getString(ValueConstant.deviceMac) ?? "";
    _deviceId.value =
        await asyncPrefs.getString(ValueConstant.deviceId) ?? uuid.v4();
    _deviceControlMode.value =
        await asyncPrefs.getInt(ValueConstant.deviceControlMode) ?? 0;
    isInitialization = true;
    isLogin.value = await asyncPrefs.getBool(ValueConstant.isLogin) ?? false;

    ///Set status bar and navigation bar to transparent
    SystemUiOverlayStyle style = SystemUiOverlayStyle(
      statusBarColor: CupertinoColors.transparent,
    );
    SystemChrome.setSystemUIOverlayStyle(style);
    SystemChrome.setEnabledSystemUIMode(.manual, overlays: [.top, .bottom]);

    ///Lock screen orientation
    SystemChrome.setPreferredOrientations([.portraitDown, .portraitUp]);

    ///Initialize package manager
    packageInfo = await PackageInfo.fromPlatform();
  }

  final uuid = Uuid();

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
  }

  bool get hasValidDeviceMac => _deviceMac.isNotEmpty;

  final RxBool _deviceIsOnline = false.obs;

  bool get deviceIsOnline => _deviceIsOnline.value;

  set deviceIsOnline(bool deviceIsOnline) {
    _deviceIsOnline.value = deviceIsOnline;
  }

  RxList<BlueDeviceInfo> blueDeviceList = RxList([]);

  DateTime? manualShutdownTime;

  bool popupState = false; //Popup state

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

  ///Location info
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

  void webSocketMessageMonitoring() {
    WebSocketUtil.shared.addObserver("App", (message) {
      if (message is Uint8List) {
        final result = parseMessage(message);
        final msgType = result.$1;
        if (msgType != null) {
          switch (msgType) {
            case .deviceOnline:
              deviceIsOnline = true;
              break;
            case .deviceOffline:
              deviceIsOnline = false;
              break;
            default:
              break;
          }
        }
      } else if (message is String) {
              }
    });
  }

  void connectWebSocket() {
    final webSocketUrl =
        "${Urls.getWebSocketUrl()}?deviceType=App&deviceId=${AppState.shared.deviceId}";
    WebSocketUtil.shared.connect(webSocketUrl);
  }

  ///Filter devices for pairing only
  List<BlueDeviceInfo> screeningDevices(List<BlueDeviceInfo> devices) {
    List<BlueDeviceInfo> newDevices = [];
    for (final deviceInfo in devices) {
      final List<dynamic>? serviceUuids =
          deviceInfo.advertisementData[ValueConstant.serviceUuids];
      if (serviceUuids == null || serviceUuids.isEmpty) {
        continue;
      }
      final bool containsTargetUUID = serviceUuids
          .map((uuid) => uuid.toString().toUpperCase())
          .contains(BlueUtil.targetServiceUUID.toUpperCase());
      if (containsTargetUUID) {
        newDevices.add(deviceInfo);
      }
    }
    return newDevices;
  }

  ///Upgrade device info
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
        getDevices();
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

  //Show bind window
  void showBindingDevice(BuildContext context) async {
    if (AppState.shared.popupState) {
      return;
    }
    if (AppState.shared.isLogin.value) {
      BlueUtil.shared.blueMode = 3;
      AppState.shared.popupState = true;
      await showCupertinoSheet(
        useNestedNavigation: true,
        context: context,
        builder: (context) {
          return BindingDevice();
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
    await showCupertinoSheet(
      context: context,
      useNestedNavigation: true,
      enableDrag: false,
      builder: (context) {
        return LoginPage();
      },
    );
    AppState.shared.popupState = false;
  }

  //Query XiaoZhi configuration status
  Future<void> startSearchXiaoZhiConfig(String mac) async {}

  RxList<Device> devices = RxList([]);

  //Get bound device list
  Future<void> getDevices() async {
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
    final map = {ValueConstant.mac: mac};
    final response = await Http.instance.post(Urls.v2deviceBind, data: map);
    if (response.data != null) {
      Model<bool> responseData = Model.fromJsonT(response.data);
      if (responseData.isSuccess()) {
        showToast("Device binding successful");
        getDevices();
        getDeviceInfo();

        ///Proactively reset device default configuration
        Http.instance.post(Urls.deviceAgentRestore, data: map);
        return true;
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
        getDevices();
      } else {
        AppState.shared.showToast(responseData.message);
      }
    }
  }

  ///Get current phone location
  Future<void> obtainLocation() async {
    try {
      //Check if location service is enabled
      bool serviceEnabled = await Geolocator.isLocationServiceEnabled();
      if (!serviceEnabled) {
        showToast(
          "Location service is not enabled. Please enable the location service first",
        );
        isLocationAvailable.value = false;
        return;
      }

      //Check location permission
      LocationPermission permission = await Geolocator.checkPermission();
      if (permission == LocationPermission.denied) {
        permission = await Geolocator.requestPermission();
        if (permission == LocationPermission.denied) {
          showToast(
            "The location permission was denied, and thus the location information could not be obtained",
          );
          isLocationAvailable.value = false;
          //Guide user to settings page to enable permission
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

      //Get current location
      Position position = await Geolocator.getCurrentPosition(
        locationSettings: const LocationSettings(
          accuracy: LocationAccuracy.high,
          distanceFilter: 100,
        ),
      );

      currentLocation.value = position;
      isLocationAvailable.value = true;
          } catch (e) {
      showToast("Failed to obtain location: ${e.toString()}");
      isLocationAvailable.value = false;
          }
  }

  ///Continuously listen for location changes
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
                isLocationAvailable.value = false;
      },
    );
  }
}

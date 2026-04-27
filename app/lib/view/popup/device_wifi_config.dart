/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:convert';
import 'dart:io';

import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';
import 'package:network_info_plus/network_info_plus.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:stack_chan/util/native_bridge.dart';
import 'package:stack_chan/view/app.dart';

import '../../app_state.dart';
import '../../model/blue_model.dart';
import '../../util/blue_util.dart';

class DeviceWifiConfig extends StatefulWidget {
  const DeviceWifiConfig({super.key, this.isWelCome});

  final bool? isWelCome;

  @override
  State<StatefulWidget> createState() => _DeviceWifiConfigState();
}

class WifiCacheKeys {
  static const String wifiName = 'cached_wifi_name';
  static const String wifiPassword = 'cached_wifi_password';
}

class _DeviceWifiConfigState extends State<DeviceWifiConfig> {
  String _wifiName = "";
  String _wifiPassword = "";

  late TextEditingController _nameTextEditingController;
  late TextEditingController _passwordTextEditingController;

  @override
  void initState() {
    super.initState();
    _nameTextEditingController = TextEditingController(text: "");
    _passwordTextEditingController = TextEditingController(text: "");
    _onAppear();
    _initializeWifiInfo();
    _registerNativeHandler();
  }

  /// Register native handler for iOS WiFi name callback
  void _registerNativeHandler() {
    NativeBridge.shared.registerHandler(Method.wifiName, (message) async {
      if (message.arguments is String) {
        final wifiName = message.arguments as String;
        if (wifiName.isNotEmpty) {
          setState(() {
            _wifiName = wifiName;
            _nameTextEditingController.text = wifiName;
          });
        }
      }
    });
  }

  /// Save WiFi information to local cache
  Future<void> _saveWifiInfoToCache(String name, String password) async {
    try {
      await AppState.asyncPrefs.setString(WifiCacheKeys.wifiName, name);
      await AppState.asyncPrefs.setString(WifiCacheKeys.wifiPassword, password);
      debugPrint("WiFi info cached: $name");
    } catch (e) {
      debugPrint("Failed to save WiFi cache: $e");
      AppState.shared.showToast("Failed to save WiFi information");
    }
  }

  /// Load WiFi information from local cache
  /// Returns true if cache exists and is valid
  Future<bool> _loadCachedWifiInfo() async {
    try {
      final cachedName =
          await AppState.asyncPrefs.getString(WifiCacheKeys.wifiName) ?? "";
      final cachedPassword =
          await AppState.asyncPrefs.getString(WifiCacheKeys.wifiPassword) ?? "";

      if (cachedName.isNotEmpty) {
        setState(() {
          _wifiName = cachedName;
          _wifiPassword = cachedPassword;
          _nameTextEditingController.text = cachedName;
          _passwordTextEditingController.text = cachedPassword;
        });
        debugPrint("Using cached WiFi info: $cachedName");
        return true;
      }
      return false;
    } catch (e) {
      debugPrint("Failed to load WiFi cache: $e");
      return false;
    }
  }

  /// Initialize WiFi info - cache first, then system if no cache
  Future<void> _initializeWifiInfo() async {
    final hasCache = await _loadCachedWifiInfo();
    if (!hasCache) {
      debugPrint("No WiFi cache found, fetching from system");
      await _fetchSystemWifiName();
    }
  }

  /// Fetch WiFi name from system with platform-specific handling
  Future<void> _fetchSystemWifiName() async {
    if (Platform.isAndroid) {
      // Android: Request permission first, then fetch if granted
      final status = await Permission.location.request();
      if (status.isGranted) {
        await _fetchAndroidWifiName();
      } else {
        debugPrint("Location permission denied, cannot fetch WiFi name");
      }
    } else if (Platform.isIOS) {
      // iOS: Request permission and immediately fetch via native bridge
      // (no need to wait for permission result on iOS)
      await Permission.locationWhenInUse.request();
      NativeBridge.shared.sendMessage(Method.wifiName);
    }
  }

  /// Fetch WiFi name on Android using network_info_plus library
  Future<void> _fetchAndroidWifiName() async {
    try {
      final networkInfo = NetworkInfo();
      String? wifiName = await networkInfo.getWifiName();

      if (wifiName != null && wifiName.isNotEmpty) {
        final cleanWifiName = wifiName.replaceAll('"', '');
        if (cleanWifiName.isNotEmpty && cleanWifiName != "unknown ssid") {
          setState(() {
            _wifiName = cleanWifiName;
            _nameTextEditingController.text = cleanWifiName;
          });
          debugPrint("Fetched Android WiFi name: $cleanWifiName");
        }
      }
    } on PlatformException catch (e) {
      debugPrint("Failed to fetch Android WiFi name: $e");
    }
  }

  @override
  void dispose() {
    NativeBridge.shared.unregisterHandler(Method.wifiName);
    _nameTextEditingController.dispose();
    _passwordTextEditingController.dispose();
    BlueUtil.shared.characteristicCallback = null;
    BlueUtil.shared.wifiSetCharacteristicCall = null;
    BlueUtil.shared.onReconnectSuccess = null;
    if (mounted) {
      FocusScope.of(context).unfocus();
    }
    super.dispose();
  }

  bool isSuccess = false;

  void dismiss() {
    AppState.shared.manualShutdownTime = DateTime.now();
    BlueUtil.shared.characteristicCallback = null;
    BlueUtil.shared.wifiSetCharacteristicCall = null;
    BlueUtil.shared.onReconnectSuccess = null;
    BlueUtil.shared.disconnectCurrentPeripheral();

    if (widget.isWelCome == true) {
      CupertinoSheetRoute.popSheet(context);
    } else {
      Navigator.of(context).pop();
    }
  }

  void _onAppear() {
    BlueUtil.shared.wifiSetCharacteristicCall = (data) {
      String json = utf8.decode(data);
      final model = BlueNotifyStateModel.fromJson(json);
      if (model?.data?.state != null) {
        String state = model!.data!.state!;
        if (state == "wifiConnecting") {
          setState(() {});
        } else if (state == "wifiConnected") {
          if (isSuccess) {
            return;
          }
          isSuccess = true;
          setState(() {});
          dismiss();
        } else if (state == "wifiConnectFailed") {
          setState(() {});
          Future.delayed(const Duration(seconds: 1), () {
            if (mounted) {
              showCupertinoDialog(
                context: context,
                builder: (context) {
                  return CupertinoAlertDialog(
                    title: const Text("Configuration failed"),
                    content: const Text(
                      "Configuration failed, please re-enter wifi name and password",
                    ),
                    actions: [
                      CupertinoDialogAction(
                        child: const Text("OK"),
                        onPressed: () {
                          Navigator.of(context).pop();
                          _passwordTextEditingController.clear();
                          FocusScope.of(context).requestFocus(FocusNode());
                        },
                      ),
                    ],
                  );
                },
              );
            }
          });
        }
      }
    };
  }

  @override
  Widget build(BuildContext context) {
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      child: CustomScrollView(
        slivers: [
          CupertinoSliverNavigationBar(
            largeTitle: Text("Wi-Fi Info"),
            leading: CupertinoNavigationBarBackButton(
              color: CupertinoColors.systemOrange,
              onPressed: () {
                Navigator.of(context).pop();
              },
            ),
            trailing: CupertinoButton(
              sizeStyle: .large,
              onPressed: () {
                TextInput.finishAutofillContext();
                confirmWifi();
              },
              child: Icon(CupertinoIcons.check_mark),
            ),
          ),
          SliverList.list(
            children: [
              Padding(
                padding: .only(left: 20, right: 20),
                child: Text(
                  "Input the Wi-Fi in your environment for StackChan to connect to. Compatible with 2.4GHz, not 5GHz.",
                  style: TextStyle(
                    fontSize: 15,
                    color: CupertinoColors.placeholderText.resolveFrom(context),
                  ),
                ),
              ),
              CupertinoListSection.insetGrouped(
                header: Text("Wi-Fi Name"),
                children: [
                  CupertinoListTile(
                    padding: .only(left: 10, right: 10),
                    title: CupertinoTextField(
                      controller: _nameTextEditingController,
                      decoration: BoxDecoration(),
                      autofocus: true,
                      textAlign: .start,
                      textInputAction: .next,
                      onChanged: (value) {
                        _wifiName = value;
                      },
                    ),
                  ),
                ],
              ),
              CupertinoListSection.insetGrouped(
                header: Text("Wi-Fi Password"),
                children: [
                  CupertinoListTile(
                    padding: .only(left: 10, right: 10),
                    title: CupertinoTextField(
                      controller: _passwordTextEditingController,
                      decoration: BoxDecoration(),
                      textAlign: .start,
                      textInputAction: .done,
                      onSubmitted: (value) {
                        confirmWifi();
                      },
                      onChanged: (value) {
                        _wifiPassword = value;
                      },
                    ),
                  ),
                ],
              ),
            ],
          ),
        ],
      ),
    );
  }

  void confirmWifi() async {
    if (_wifiName.isEmpty || _wifiPassword.isEmpty) {
      AppState.shared.showToast("Please enter the full name and password");
      return;
    }

    _saveWifiInfoToCache(_wifiName, _wifiPassword);

    //savewifiinfo
    final BlueWifiModel model = BlueWifiModel(
      cmd: "setWifi",
      data: BlueWifi(ssid: _wifiName, password: _wifiPassword),
    );
    final jsonString = model.toJson();
    if (jsonString != null) {
      final result = await BlueUtil.shared.sendWifiSetData(jsonString);
      if (!result) {
        dismiss();
        App.showDialog(
          "Bluetooth disconnected. Please re-pair WiFi on your StackChan.",
        );
      }
    }
  }
}

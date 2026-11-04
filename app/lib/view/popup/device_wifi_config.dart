/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:flutter/services.dart';
import 'package:network_info_plus/network_info_plus.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:stack_chan/util/native_bridge.dart';
import 'package:stack_chan/view/app.dart';

import '../../app_state.dart';
import '../../model/blue_model.dart';
import '../../util/blue_util.dart';
import '../util/popup.dart';
import '../util/theme_setting.dart';

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

  /// Wi-Fi the phone itself is connected to, used as a band hint for the
  /// firmware. It stays empty when the system does not expose the SSID.
  String _phoneWifiName = "";

  late TextEditingController _nameTextEditingController;
  late TextEditingController _passwordTextEditingController;

  static const int _wifiNotifyMaxBytes = 8 * 1024;
  static const Duration _wifiNotifyFragmentTimeout = Duration(seconds: 2);
  final List<int> _wifiNotifyBytes = <int>[];
  Timer? _wifiNotifyFragmentTimer;

  @override
  void initState() {
    super.initState();
    _nameTextEditingController = TextEditingController(text: "");
    _passwordTextEditingController = TextEditingController(text: "");
    _onAppear();
    _initializeWifiInfo();
    _registerNativeHandler();
    BlueUtil.shared.characteristicCallback = (device, characteristic) {
      if (characteristic.uuid == BlueUtil.wifiSetCharacteristicUUID) {
        _prepareAndroidMtu(device);
      }
    };
  }

  /// Registers the native handler that delivers the iOS Wi-Fi name.
  void _registerNativeHandler() {
    NativeBridge.shared.registerHandler(Method.wifiName, (message) async {
      if (message.arguments is String) {
        final wifiName = message.arguments as String;
        if (wifiName.isNotEmpty) {
          setState(() {
            _phoneWifiName = wifiName;
            _wifiName = wifiName;
            _nameTextEditingController.text = wifiName;
          });
        }
      }
    });
  }

  /// Saves the Wi-Fi credentials to the local cache.
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

  /// Loads the Wi-Fi credentials from the local cache.
  /// Returns true when a cached SSID was found.
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

  /// Loads Wi-Fi info from the cache, then from the system if needed.
  Future<void> _initializeWifiInfo() async {
    final hasCache = await _loadCachedWifiInfo();
    if (!hasCache) {
      debugPrint("No WiFi cache found, fetching from system");
      await _fetchSystemWifiName();
    }
  }

  /// Fetches the Wi-Fi name using platform-specific APIs.
  Future<void> _fetchSystemWifiName() async {
    if (Platform.isAndroid) {
      // Android only exposes the SSID once location permission is granted.
      final status = await Permission.location.request();
      if (status.isGranted) {
        await _fetchAndroidWifiName();
      } else {
        debugPrint("Location permission denied, cannot fetch WiFi name");
      }
    } else if (Platform.isIOS) {
      // iOS returns the SSID through the native bridge, so the request is
      // sent without waiting for the permission result.
      await Permission.locationWhenInUse.request();
      NativeBridge.shared.sendMessage(Method.wifiName);
    }
  }

  /// Fetches the Wi-Fi name on Android through network_info_plus.
  Future<void> _fetchAndroidWifiName() async {
    try {
      final networkInfo = NetworkInfo();
      String? wifiName = await networkInfo.getWifiName();

      if (wifiName != null && wifiName.isNotEmpty) {
        final cleanWifiName = wifiName.replaceAll('"', '');
        if (cleanWifiName.isNotEmpty && cleanWifiName != "unknown ssid") {
          setState(() {
            _phoneWifiName = cleanWifiName;
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
    FocusManager.instance.primaryFocus?.unfocus();
    NativeBridge.shared.unregisterHandler(Method.wifiName);
    _nameTextEditingController.dispose();
    _passwordTextEditingController.dispose();
    BlueUtil.shared.characteristicCallback = null;
    BlueUtil.shared.wifiSetCharacteristicCall = null;
    BlueUtil.shared.onReconnectSuccess = null;
    _wifiNotifyFragmentTimer?.cancel();
    _wifiNotifyBytes.clear();
    super.dispose();
  }

  bool isSuccess = false;
  bool _isSubmittingWifi = false;

  void dismiss() {
    AppState.shared.manualShutdownTime = DateTime.now();
    BlueUtil.shared.characteristicCallback = null;
    BlueUtil.shared.wifiSetCharacteristicCall = null;
    BlueUtil.shared.onReconnectSuccess = null;
    BlueUtil.shared.disconnectCurrentPeripheral();

    if (widget.isWelCome == true) {
      closeCustomCupertinoSheet(context);
    } else {
      Navigator.maybePop(context);
    }
  }

  void _onAppear() {
    BlueUtil.shared.wifiSetCharacteristicCall = (data) {
      final messages = _appendWifiNotifyData(data);
      for (final message in messages) {
        _handleWifiSetNotification(message);
      }
    };
  }

  Future<void> _prepareAndroidMtu(dynamic device) async {
    if (!Platform.isAndroid) {
      return;
    }

    const List<int> mtuFallbackList = [247, 185, 158, 100, 69];
    for (final mtu in mtuFallbackList) {
      try {
        final negotiatedMtu = await device.requestMtu(mtu);
        debugPrint(
          "✅ [WiFi Config] MTU negotiated for config characteristic: target=$mtu, actual=$negotiatedMtu",
        );
        break;
      } catch (e) {
        debugPrint("⚠️ [WiFi Config] MTU request $mtu failed: $e");
      }
    }
  }

  List<String> _appendWifiNotifyData(List<int> data) {
    _wifiNotifyFragmentTimer?.cancel();
    _wifiNotifyBytes.addAll(data);

    if (_wifiNotifyBytes.length > _wifiNotifyMaxBytes) {
      debugPrint("⚠️ DeviceNotification buffer too large, dropped");
      _resetWifiNotifyBuffer();
      return const <String>[];
    }

    String text;
    try {
      text = utf8.decode(_wifiNotifyBytes);
    } catch (e) {
      debugPrint("📥 DeviceNotification UTF-8 分包接收中: $e");
      _scheduleWifiNotifyBufferCleanup();
      return const <String>[];
    }

    final extractResult = _extractCompleteJsonObjects(text);
    if (extractResult.consumedCharacters > 0) {
      final consumedText = text.substring(0, extractResult.consumedCharacters);
      final consumedBytes = utf8.encode(consumedText).length;
      final safeConsumedBytes = consumedBytes > _wifiNotifyBytes.length
          ? _wifiNotifyBytes.length
          : consumedBytes;
      _wifiNotifyBytes.removeRange(0, safeConsumedBytes);
    }

    if (extractResult.messages.isEmpty) {
      if (_wifiNotifyBytes.isNotEmpty) {
        debugPrint(
          "📥 DeviceNotification 分包接收中: ${_wifiNotifyBytes.length} bytes",
        );
        _scheduleWifiNotifyBufferCleanup();
      }
      return const <String>[];
    }

    if (_wifiNotifyBytes.isNotEmpty) {
      _scheduleWifiNotifyBufferCleanup();
    }

    return extractResult.messages;
  }

  _JsonExtractResult _extractCompleteJsonObjects(String text) {
    final messages = <String>[];
    var start = -1;
    var depth = 0;
    var inString = false;
    var escaping = false;
    var consumedCharacters = 0;

    for (var i = 0; i < text.length; i++) {
      final codeUnit = text.codeUnitAt(i);

      if (start < 0) {
        if (codeUnit == 0x7B) {
          start = i;
          depth = 1;
        }
        continue;
      }

      if (inString) {
        if (escaping) {
          escaping = false;
        } else if (codeUnit == 0x5C) {
          escaping = true;
        } else if (codeUnit == 0x22) {
          inString = false;
        }
        continue;
      }

      if (codeUnit == 0x22) {
        inString = true;
      } else if (codeUnit == 0x7B) {
        depth++;
      } else if (codeUnit == 0x7D) {
        depth--;
        if (depth == 0) {
          messages.add(text.substring(start, i + 1));
          consumedCharacters = i + 1;
          start = -1;
        }
      }
    }

    if (start < 0 && text.isNotEmpty) {
      consumedCharacters = text.length;
    } else if (messages.isEmpty && start > 0) {
      consumedCharacters = start;
    }

    return _JsonExtractResult(messages, consumedCharacters);
  }

  void _scheduleWifiNotifyBufferCleanup() {
    _wifiNotifyFragmentTimer?.cancel();
    _wifiNotifyFragmentTimer = Timer(_wifiNotifyFragmentTimeout, () {
      if (_wifiNotifyBytes.isEmpty) return;
      debugPrint(
        "⚠️ DeviceNotification 分包超时，丢弃 ${_wifiNotifyBytes.length} bytes",
      );
      _resetWifiNotifyBuffer();
    });
  }

  void _resetWifiNotifyBuffer() {
    _wifiNotifyFragmentTimer?.cancel();
    _wifiNotifyFragmentTimer = null;
    _wifiNotifyBytes.clear();
  }

  void _handleWifiSetNotification(String json) {
    final model = BlueNotifyStateModel.fromJson(json);
    if (model?.data?.state == null) {
      return;
    }

    final String state = model!.data!.state!;
    if (state == "wifiConnecting") {
      setState(() {});
      return;
    }

    if (state == "wifiConnected") {
      if (isSuccess) {
        return;
      }
      isSuccess = true;
      setState(() {});
      dismiss();
      return;
    }

    if (state.startsWith("wifiConnectFailed")) {
      final failure = model.data!.wifiFailureDescription;
      setState(() {});
      Future.delayed(const Duration(seconds: 1), () {
        if (mounted) {
          showCupertinoDialog(
            context: context,
            builder: (context) {
              return CupertinoAlertDialog(
                title: const Text("Configuration failed"),
                content: Text(failure),
                actions: [
                  CupertinoDialogAction(
                    child: const Text("OK"),
                    onPressed: () {
                      Navigator.maybePop(context);
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

  @override
  Widget build(BuildContext context) {
    final padding = MediaQuery.paddingOf(context);

    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      child: CustomScrollView(
        slivers: [
          CupertinoSliverNavigationBar(
            backgroundColor: ThemeSetting.cupertinoNavigationBarBackgroundColor(
              context,
            ),
            border: ThemeSetting.cupertinoNavigationBarBorder(context),
            padding: ThemeSetting.navigationBarPadding,
            largeTitle: const Text("Wi-Fi Info"),
            stretch: true,
            leading: widget.isWelCome == true
                ? const CupertinoNavigationBarBackButton()
                : const SizedBox.shrink(),
            trailing: _isSubmittingWifi
                ? const CupertinoActivityIndicator()
                : CupertinoButton(
                    sizeStyle: .medium,
                    onPressed: () {
                      TextInput.finishAutofillContext();
                      unawaited(confirmWifi());
                    },
                    child: const Icon(CupertinoIcons.check_mark),
                  ),
          ),
          SliverPadding(
            padding: .only(left: padding.left, right: padding.right),
            sliver: SliverList.list(
              children: [
                Padding(
                  padding: const .only(left: 20, right: 20),
                  child: Text(
                    "Input the Wi-Fi in your environment for StackChan to connect to. Compatible with 2.4GHz only.",
                    style: TextStyle(
                      fontSize: 15,
                      color: CupertinoColors.placeholderText.resolveFrom(
                        context,
                      ),
                    ),
                  ),
                ),
                CupertinoListSection.insetGrouped(
                  header: const Text("Wi-Fi SSID"),
                  children: [
                    CupertinoTextField(
                      controller: _nameTextEditingController,
                      decoration: const BoxDecoration(),
                      autofocus: true,
                      padding: const .all(12),
                      textAlign: .start,
                      textInputAction: .next,
                      onChanged: (value) {
                        _wifiName = value;
                      },
                    ),
                  ],
                ),
                CupertinoListSection.insetGrouped(
                  header: const Text("Wi-Fi Password"),
                  children: [
                    CupertinoTextField(
                      controller: _passwordTextEditingController,
                      decoration: const BoxDecoration(),
                      textAlign: .start,
                      padding: const .all(12),
                      textInputAction: .done,
                      keyboardType: .visiblePassword,
                      onSubmitted: (value) {
                        unawaited(confirmWifi());
                      },
                      onChanged: (value) {
                        _wifiPassword = value;
                      },
                    ),
                  ],
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Future<void> confirmWifi() async {
    if (_isSubmittingWifi || isSuccess) {
      return;
    }

    if (_wifiName.isEmpty) {
      AppState.shared.showToast("Please enter the full name and password");
      return;
    }

    setState(() {
      _isSubmittingWifi = true;
    });

    try {
      final currentDevice = BlueUtil.shared.currentPeripheral;
      if (Platform.isAndroid && currentDevice != null) {
        await _prepareAndroidMtu(currentDevice);
      }

      await _saveWifiInfoToCache(_wifiName, _wifiPassword);

      // Tell the firmware when the target SSID is the phone's own network, so a
      // network the phone can see but the device cannot is reported as 5 GHz.
      final bool isCurrentNetwork =
          _phoneWifiName.isNotEmpty && _phoneWifiName == _wifiName;

      // Send the credentials to the device.
      final BlueWifiModel model = BlueWifiModel(
        cmd: "setWifi",
        data: BlueWifi(
          ssid: _wifiName,
          password: _wifiPassword,
          currentNetwork: isCurrentNetwork,
        ),
      );
      final jsonString = model.toJson();
      if (jsonString != null) {
        final result = await BlueUtil.shared.sendWifiSetData(jsonString);
        if (!mounted || isSuccess) {
          return;
        }
        if (!result.success) {
          dismiss();
          App.showDialog(
            "Wi-Fi configuration failed: ${result.displayMessage}",
          );
        }
      }
    } finally {
      if (mounted && !isSuccess) {
        setState(() {
          _isSubmittingWifi = false;
        });
      } else {
        _isSubmittingWifi = false;
      }
    }
  }
}

class _JsonExtractResult {
  const _JsonExtractResult(this.messages, this.consumedCharacters);

  final List<String> messages;
  final int consumedCharacters;
}

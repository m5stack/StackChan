/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/theme_setting.dart';
import 'package:stack_chan/native/native_bluetooth_bridge.dart';
import 'package:flutter_svg/svg.dart';
import 'package:get/get.dart';
import 'package:stack_chan/util/rsa_util.dart';
import 'package:stack_chan/view/popup/login_page.dart';

import '../../app_state.dart';
import '../../model/blue_device_info.dart';
import '../../model/blue_model.dart';
import '../../util/blue_util.dart';
import '../../util/value_constant.dart';
import '../util/popup.dart';
import 'device_name_page.dart';

/// Bluetooth scanning and device-binding sheet.
class SelectBlueDevice extends StatefulWidget {
  const SelectBlueDevice({super.key});

  @override
  State<StatefulWidget> createState() => _SelectBlueDeviceState();
}

class _SelectBlueDeviceState extends State<SelectBlueDevice> {
  Timer? _connectTimer;

  Timer? _verifyTimer;

  //timeouttime:30Second(s)
  static const int _connectTimeout = 30;

  static const int _verifyTimeout = 20;
  static const int _wifiNotifyMaxBytes = 8 * 1024;
  static const Duration _wifiNotifyFragmentTimeout = Duration(seconds: 2);

  final List<int> _wifiNotifyBytes = <int>[];
  Timer? _wifiNotifyFragmentTimer;

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

  RxString connectDeviceId = RxString("");

  @override
  void dispose() {
    _connectTimer?.cancel();
    _verifyTimer?.cancel();
    _resetWifiNotifyBuffer();
    BlueUtil.shared.blufDevicesMonitoring = null;
    BlueUtil.shared.characteristicCallback = null;
    BlueUtil.shared.wifiSetCharacteristicCall = null;
    BlueUtil.shared.onReconnectSuccess = null;
    connectDeviceId.value = "";
    isSuccess = false;
    super.dispose();
  }

  bool isSuccess = false;

  Future<void> _preparePairingMtu(NativeBluetoothDevice device) async {
    if (Platform.isIOS) {
      try {
        final effectiveMtu = await device.requestMtu(185);
        debugPrint("ℹ️ iOS 使用系统协商 MTU，当前可用 MTU: $effectiveMtu");
      } catch (e) {
        debugPrint("⚠️ iOS 读取 MTU 信息失败，继续使用系统默认值: $e");
      }
      return;
    }

    if (Platform.isAndroid) {
      await _ensureAndroidMtu(device);
    }
  }

  Future<void> _ensureAndroidMtu(NativeBluetoothDevice device) async {
    // 512 often fails to negotiate or yields unstable packet reception on low-end
    // Android devices, so prefer more common MTUs that are known to be stable.
    // 23 is the BLE default MTU and needs no explicit request.
    const List<int> mtuFallbackList = [247, 185, 158, 100, 69];
    bool mtuNegotiated = false;

    for (final targetMtu in mtuFallbackList) {
      try {
        final negotiatedMtu = await device.requestMtu(targetMtu);
        mtuNegotiated = true;
        debugPrint("✅ MTU 协商成功，目标: $targetMtu，实际: $negotiatedMtu");
        if (negotiatedMtu < targetMtu) {
          debugPrint("ℹ️ 实际 MTU 低于目标值，将依赖通知分包拼接保证完整数据");
        }
        break;
      } catch (e) {
        debugPrint("⚠️ MTU 尝试 $targetMtu 失败: $e");
      }
    }

    if (!mtuNegotiated) {
      debugPrint("⚠️ MTU 协商失败，继续使用系统默认 MTU(通常为 23)");
    }
  }

  @override
  void initState() {
    super.initState();
    // Enter binding mode and set the binding flag.
    BlueUtil.shared.blueMode = 3;
    _registerDeviceScanCallbacks();
    _registerBlueCallbacks();
    unawaited(BlueUtil.shared.ensureBluetoothReady(triggerScan: true));
    BlueUtil.shared.onReconnectSuccess = (device) async {
      if (mounted) {
        AppState.shared.showToast("Reconnected to device successfully.");
      }
      await _preparePairingMtu(device);
      _registerBlueCallbacks();
      //reconnectafterretriggerdeviceverify（key：originallogiconlyhasfirstconnectwhentrigger）
      if (BlueUtil.shared.writeWifiSetCharacteristic != null) {
        await startVerificationEquipment(
          BlueUtil.shared.writeWifiSetCharacteristic!,
        );
      }
    };
  }

  void _registerDeviceScanCallbacks() {
    BlueUtil.shared.blufDevicesMonitoring = (devices) {
      if (!mounted) {
        return;
      }
      _updatePairingDevices(devices);
    };
    _updatePairingDevices(BlueUtil.shared.discoveredDevices);
  }

  void _updatePairingDevices(List<BlueDeviceInfo> devices) {
    AppState.shared.blueDeviceList.value = AppState.shared.screeningDevices(
      devices,
    );
  }

  //resetconnectstate(System1wrap)
  void _resetConnectState() {
    connectDeviceId.value = "";
    _connectTimer?.cancel();
    _verifyTimer?.cancel();
    _resetWifiNotifyBuffer();
    if (mounted) {
      setState(() {});
    }
  }

  void _registerBlueCallbacks() {
    BlueUtil.shared.characteristicCallback = (device, characteristic) async {
      final properties = characteristic.properties;
      final canWrite = properties.write || properties.writeWithoutResponse;
      if (canWrite) {
        final uuid = characteristic.uuid.toString();
        if (uuid == BlueUtil.wifiSetCharacteristicUUID) {
          BlueUtil.shared.writeWifiSetCharacteristic = characteristic;
          debugPrint(" DeviceConfigurationCharacteristicReady");
          await _preparePairingMtu(device);
          await startVerificationEquipment(characteristic);
          return;
        }
      }
      debugPrint(" Not找到WiFiConfigurationCharacteristic");
      if (mounted) {
        AppState.shared.showToast(
          "Pairing service not found. Restart StackChan in binding mode and try again.",
        );
      }
    };

    BlueUtil.shared.wifiSetCharacteristicCall = (data) async {
      final jsonMessages = _appendWifiNotifyData(data);
      for (final json in jsonMessages) {
        await _handleWifiSetNotification(json);
      }
    };
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
      _wifiNotifyBytes.clear();
    });
  }

  void _resetWifiNotifyBuffer() {
    _wifiNotifyFragmentTimer?.cancel();
    _wifiNotifyFragmentTimer = null;
    _wifiNotifyBytes.clear();
  }

  Future<void> _handleWifiSetNotification(String json) async {
    try {
      debugPrint("📥 收到DeviceNotification: $json");
      final model = BlueNotifyStateModel.fromJson(json);

      if (model == null) {
        debugPrint(" DeviceDataParseFailed");
        if (mounted) {
          AppState.shared.showToast(
            "Could not read StackChan's pairing response. Keep the device nearby and try again.",
          );
        }
        return;
      }

      if (model.cmd != null &&
          model.cmd == "notifyState" &&
          model.data?.type == 4) {
        // Read the identity encryption payload.
        String? data = model.data?.state;
        if (data == null) {
          debugPrint(" DeviceNotReturnEncryptInfo");
          if (mounted) {
            AppState.shared.showToast(
              "StackChan returned incomplete verification data. Restart binding mode and try again.",
            );
          }
          return;
        }

        // Decrypt the RSA-encrypted device info.
        final result = RsaUtil.decryptStackChanBlue(data);
        // Decryption failed or the payload is shorter than the expected MAC prefix.
        if (result.isEmpty || result.length < 12) {
          debugPrint(" DeviceInfoDecryptFailed");
          if (mounted) {
            AppState.shared.showToast(
              "StackChan's verification data was invalid. Restart binding mode and try again.",
            );
          }
          return;
        }

        if (isSuccess) return;
        isSuccess = true;
        _connectTimer?.cancel(); // Cancel the connection timeout
        _verifyTimer?.cancel(); // Cancel the verification timeout
        _resetWifiNotifyBuffer();

        final macAddress = result.substring(0, 12);

        AppState.shared.showToast("Detected device MAC address: $macAddress");
        await _activateAndBindDevice(macAddress);
      }
    } catch (e) {
      debugPrint(" HandleDeviceNotificationFailed: $e");
      if (mounted) {
        AppState.shared.showToast(
          "Could not process StackChan's pairing response. Restart binding mode and try again.",
        );
      }
    }
  }

  Future<void> _activateAndBindDevice(String macAddress) async {
    try {
      debugPrint(" DeviceActivateBindStart, mac: $macAddress");
      bool result = await AppState.shared.bindDevice(macAddress);
      _resetConnectState();
      if (result) {
        if (mounted) {
          AppState.shared.showToast("Device bound successfully!");
          Navigator.of(context).push(
            CupertinoPageRoute(builder: (context) => const DeviceNamePage()),
          );
        }
      } else {
        debugPrint(" DeviceActivateBindFailed, mac: $macAddress");
        if (mounted) {
          AppState.shared.showToast(
            "Verification succeeded, but binding failed. Check your internet connection and account, then try again.",
          );
        }
      }
    } catch (e, stackTrace) {
      _resetConnectState();
      debugPrint(
        " DeviceActivateBindProcess / ThreadException, mac: $macAddress, error: $e",
      );
      debugPrint(" DeviceActivateBindStackTrace: $stackTrace");
      if (mounted) {
        AppState.shared.showToast(
          "Binding failed unexpectedly. Check your internet connection and try again.",
        );
      }
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
            largeTitle: const Text("Select Device"),
            stretch: true,
            leading: const SizedBox.shrink(),
            trailing: CupertinoButton(
              sizeStyle: .medium,
              child: const Icon(CupertinoIcons.xmark),
              onPressed: () {
                AppState.shared.manualShutdownTime = DateTime.now();
                BlueUtil.shared.characteristicCallback = null;
                BlueUtil.shared.wifiSetCharacteristicCall = null;
                BlueUtil.shared.onReconnectSuccess = null;
                closeCustomCupertinoSheet(context);
              },
            ),
          ),
          SliverList.list(
            children: [
              Obx(
                () => Padding(
                  padding: .only(
                    left: padding.left + 20,
                    top: 20,
                    right: padding.right + 20,
                    bottom: 10,
                  ),
                  child: ClipRRect(
                    borderRadius: .circular(ThemeSetting.cardRadius),
                    child: CupertinoListSection.insetGrouped(
                      margin: .zero,
                      children: AppState.shared.blueDeviceList.isNotEmpty
                          ? AppState.shared.blueDeviceList.indexed.map((entry) {
                              final deviceInfo = entry.$2;
                              String? deviceId = _getDeviceId(deviceInfo);
                              return CupertinoListTile(
                                onTap: () async {
                                  if (!AppState.shared.isLogin.value) {
                                    if (mounted) {
                                      Navigator.of(context).push(
                                        CupertinoPageRoute(
                                          builder: (context) =>
                                              const LoginPage(isWelCome: true),
                                        ),
                                      );
                                    }
                                    return;
                                  }

                                  final bluetoothReady = await BlueUtil.shared
                                      .ensureBluetoothReady(triggerScan: true);
                                  if (!bluetoothReady) {
                                    if (mounted) {
                                      AppState.shared.showToast(
                                        "Bluetooth is unavailable. Turn it on and allow app Bluetooth access, then try again.",
                                      );
                                    }
                                    return;
                                  }

                                  // Ignore repeated taps while a connection is in progress.
                                  final deviceRemoteId = deviceInfo
                                      .device
                                      .remoteId
                                      .toString();
                                  if (deviceRemoteId == connectDeviceId.value) {
                                    if (mounted) {
                                      AppState.shared.showToast(
                                        "Already connecting... Please wait patiently.",
                                      );
                                    }
                                    return;
                                  }

                                  // Start connecting.
                                  isSuccess = false;
                                  _resetWifiNotifyBuffer();
                                  connectDeviceId.value = deviceRemoteId;
                                  AppState.shared.blueDeviceList.refresh();

                                  // Connection timeout timer.
                                  _connectTimer?.cancel();
                                  _connectTimer = Timer(
                                    const Duration(seconds: _connectTimeout),
                                    () {
                                      _resetConnectState();
                                      debugPrint(" DeviceConnectTimeout");
                                      if (mounted) {
                                        AppState.shared.showToast(
                                          "Connection timed out after 30 seconds. Keep StackChan nearby and in binding mode, then try again.",
                                        );
                                      }
                                    },
                                  );

                                  // Catch Bluetooth connection errors.
                                  try {
                                    if (mounted) {
                                      AppState.shared.showToast(
                                        "Connecting to device...",
                                      );
                                    }
                                    await BlueUtil.shared.connect(
                                      deviceInfo.device,
                                    );
                                  } catch (e) {
                                    _resetConnectState();
                                    debugPrint(" BluetoothConnectFailed: $e");
                                    if (mounted) {
                                      AppState.shared.showToast(
                                        "Bluetooth connection failed. Keep StackChan nearby and in binding mode, then retry. Details: ${e.toString()}",
                                      );
                                    }
                                  }
                                },
                                leading: Image.asset(
                                  "assets/image1.png",
                                  width: 28,
                                  height: 28,
                                ),
                                title: Text(deviceInfo.device.advName),
                                subtitle: deviceId != null
                                    ? Text("ID: $deviceId")
                                    : null,
                                trailing:
                                    deviceInfo.device.remoteId.toString() ==
                                        connectDeviceId.value
                                    ? const CupertinoActivityIndicator()
                                    : SvgPicture.asset(
                                        "assets/chevron.right.svg",
                                        width: 15,
                                        height: 15,
                                        colorFilter: ColorFilter.mode(
                                          CupertinoColors.secondaryLabel
                                              .resolveFrom(context),
                                          BlendMode.srcIn,
                                        ),
                                      ),
                              );
                            }).toList()
                          : [
                              const CupertinoListTile(
                                title: Text("No devices found"),
                              ),
                            ],
                    ),
                  ),
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }

  // Start device verification by sending the handshake payload.
  Future<void> startVerificationEquipment(
    NativeBluetoothCharacteristic characteristic,
  ) async {
    try {
      if (mounted) {
        AppState.shared.showToast("Verifying device...");
      }
      // Start the verification timeout timer
      _verifyTimer?.cancel();
      _verifyTimer = Timer(const Duration(seconds: _verifyTimeout), () {
        _resetConnectState();
        debugPrint(" DeviceVerifyTimeout");
        if (mounted) {
          AppState.shared.showToast(
            "Verification timed out after 20 seconds. Restart StackChan in binding mode and try again.",
          );
        }
      });
      // Build the handshake payload.
      final dateTimeString = DateTime.now().millisecondsSinceEpoch.toString();
      final BlueEncryptionDecryption data = BlueEncryptionDecryption(
        cmd: "handshake",
        data: dateTimeString,
      );
      final jsonString = jsonEncode(data.toJson());

      // Send the handshake payload.
      final result = await BlueUtil.shared.sendWifiSetData(jsonString);
      if (result.success) {
        debugPrint(" 发送DeviceVerify指令Success");
      } else {
        _verifyTimer?.cancel();
        AppState.shared.showToast(
          "Could not send the verification request: "
          "${result.displayMessage} Keep StackChan nearby and retry.",
        );
        _resetConnectState();
      }
    } catch (e) {
      _verifyTimer?.cancel();
      _resetConnectState();
      debugPrint(" 发送DeviceVerify指令Failed: $e");
      if (mounted) {
        AppState.shared.showToast(
          "Bluetooth disconnected while sending the verification request. Keep StackChan nearby and try again.",
        );
      }
    }
  }
}

class _JsonExtractResult {
  const _JsonExtractResult(this.messages, this.consumedCharacters);

  final List<String> messages;
  final int consumedCharacters;
}

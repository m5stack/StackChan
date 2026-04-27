/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/cupertino.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:flutter_svg/svg.dart';
import 'package:get/get.dart';
import 'package:stack_chan/util/XiaoZhi_util.dart';
import 'package:stack_chan/util/rsa_util.dart';
import 'package:stack_chan/view/popup/login_page.dart';

import '../../app_state.dart';
import '../../model/blue_device_info.dart';
import '../../model/blue_model.dart';
import '../../util/blue_util.dart';
import '../../util/mac_address_validator.dart';
import '../../util/value_constant.dart';
import 'device_name_page.dart';

enum DeviceWifiConfigStep { selectDevice, wifiConfig }

///scanBluetoothbinddevicepopup
class SelectBlueDevice extends StatefulWidget {
  const SelectBlueDevice({super.key});

  @override
  State<StatefulWidget> createState() => _SelectBlueDeviceState();
}

class _SelectBlueDeviceState extends State<SelectBlueDevice> {
  //connecttimeouttimer(preventNo限Wait)
  Timer? _connectTimer;

  //verifytimeouttimer(preventdeviceNoresponsewhen1直to圈)
  Timer? _verifyTimer;

  //timeouttime:30Second(s)
  static const int _connectTimeout = 30;

  //verifytimeouttime:20Second(s)
  static const int _verifyTimeout = 20;

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
    BlueUtil.shared.characteristicCallback = null;
    BlueUtil.shared.wifiSetCharacteristicCall = null;
    BlueUtil.shared.onReconnectSuccess = null;
    connectDeviceId.value = "";
    isSuccess = false;
    super.dispose();
  }

  bool isSuccess = false;

  @override
  void initState() {
    super.initState();
    //enterbindStreamProcess / Thread,setbindflag
    _registerBlueCallbacks();
    BlueUtil.shared.onReconnectSuccess = (device) {
      if (mounted) {
        AppState.shared.showToast("Reconnected to device successfully.");
      }
      _registerBlueCallbacks();
      //reconnectafterretriggerdeviceverify（key：originallogiconlyhasfirstconnectwhentrigger）
      if (BlueUtil.shared.writeWifiSetCharacteristic != null) {
        startVerificationEquipment(BlueUtil.shared.writeWifiSetCharacteristic!);
      }
    };
  }

  //resetconnectstate(System1wrap)
  void _resetConnectState() {
    connectDeviceId.value = "";
    _connectTimer?.cancel();
    _verifyTimer?.cancel();
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
          if (Platform.isAndroid) {
            await device
                .requestMtu(512)
                .then((mtu) {
                  debugPrint(" MTUSettingsSuccess: $mtu");
                })
                .catchError((e) {
                  debugPrint(" MTUSettingsFailed: $e");
                  if (mounted) {
                    AppState.shared.showToast(
                      "Failed to set MTU. Device may not work properly.",
                    );
                  }
                });
          }
          //enablefeatureValuenotify
          try {
            await characteristic.setNotifyValue(true);
            debugPrint(" 开启DeviceNotificationSuccess");
            startVerificationEquipment(characteristic);
          } catch (e) {
            debugPrint(" 开启DeviceNotificationFailed: $e");
            _resetConnectState();
            if (mounted) {
              AppState.shared.showToast(
                "Failed to enable device notification.",
              );
            }
          }
          return;
        }
      }
      debugPrint(" Not找到WiFiConfigurationCharacteristic");
      if (mounted) {
        AppState.shared.showToast("Device configuration feature not found.");
      }
    };
    BlueUtil.shared.wifiSetCharacteristicCall = (data) async {
      try {
        String json = utf8.decode(data);
        debugPrint("📥 收到DeviceNotification: $json");
        final model = BlueNotifyStateModel.fromJson(json);

        if (model == null) {
          debugPrint(" DeviceDataParseFailed");
          if (mounted) {
            AppState.shared.showToast("Failed to parse device data.");
          }
          return;
        }

        if (model.cmd != null &&
            model.cmd == "notifyState" &&
            model.data?.type == 4) {
          //拿to身份Encryptinfo
          String? data = model.data?.state;
          if (data == null) {
            debugPrint(" DeviceNotReturnEncryptInfo");
            if (mounted) {
              AppState.shared.showToast(
                "Device did not return encryption data.",
              );
            }
            return;
          }

          //RSADecrypt
          final result = RsaUtil.decryptStackChanBlue(data);
          //newIncrease / Add:Decryptfail/lengthNot足
          if (result.isEmpty || result.length < 12) {
            debugPrint(" DeviceInfoDecryptFailed");
            if (mounted) {
              AppState.shared.showToast(
                "Device verification decryption failed.",
              );
            }
            return;
          }

          if (isSuccess) return;
          isSuccess = true;
          _connectTimer?.cancel(); //cancelconnecttimeouttimer
          _verifyTimer?.cancel(); //cancelverifytimeouttimer

          final macAddress = result.substring(0, 12);

          AppState.shared.showToast(
            "The initially activated MAC address: $macAddress",
          );

          AppState.shared.deviceMac = macAddress;
          AppState.shared.connectWebSocket();
          activateDevice(macAddress);
        }
      } catch (e) {
        debugPrint(" HandleDeviceNotificationFailed: $e");
        if (mounted) {
          AppState.shared.showToast("Failed to process device data.");
        }
      }
    };
  }

  String formatMacAddress(String mac) {
    if (mac.length != 12) return mac;
    return mac
        .toUpperCase()
        .replaceAllMapped(RegExp(r'(.{2})'), (match) => '${match.group(1)}:')
        .substring(0, 17); //remove最after一个冒号
  }

  bool isValidMac(String mac) {
    final RegExp macRegex = RegExp(r'^[0-9A-Fa-f]{12}$');
    return macRegex.hasMatch(mac);
  }

  //activatedevice
  Future<void> activateDevice(String macAddress) async {
    try {
      ///startqueryagentconfiginfo
      bool isConfiguration = await queryConfiguration(macAddress);
      if (!isConfiguration) {
        _resetConnectState();
        //activatefail(Alreadyhashint,No需Repeat)
        return;
      }

      if (mounted) {
        AppState.shared.showToast("The AI Agent has been configured.");
      }

      //binddevice关系
      bool result = await AppState.shared.bindDevice(macAddress);
      if (result) {
        _resetConnectState();
        if (mounted) {
          AppState.shared.showToast("Device bound successfully!");
          //configjump
          Navigator.of(context).push(
            CupertinoPageRoute(builder: (context) => const DeviceNamePage()),
          );
        }
      } else {
        _resetConnectState();
        //newIncrease / Add:binddevicefail
        debugPrint(" DeviceBindFailed");
        if (mounted) {
          AppState.shared.showToast("Device binding failed. Please try again.");
        }
      }
    } catch (e) {
      _resetConnectState();
      debugPrint(" DeviceActivateStreamProcess / ThreadException: $e");
      if (mounted) {
        AppState.shared.showToast("Device activation exception.");
      }
    }
  }

  //querydeviceconfig(全StreamProcess / Threaderrorhint)
  Future<bool> queryConfiguration(String macAddress) async {
    try {
      //1. querydevicewhetheractivated (laterNotAgainquery directactivate)
      // final devices = await XiaoZhiUtil.shared.getDevice(macAddress);
      // if (devices.isNotEmpty) {
      //   if (devices.first.agent_id != null) {
      //debugPrint("✅ deviceactivated");
      //     return true;
      //   }
      // }

      //2. generatelicense
      final generateLicense = await XiaoZhiUtil.shared.generateLicense(
        macAddress,
      );
      if (generateLicense == null || generateLicense.serialNumber == null) {
        debugPrint(" 生成DeviceLicenseFailed");
        if (mounted) {
          AppState.shared.showToast("Failed to generate device license.");
        }
        return false;
      }

      //3. activatedevice
      final serialNumber = generateLicense.serialNumber!;
      final mac = MacAddressValidator.formatMac(AppState.shared.deviceMac);
      if (mac == null) {
        AppState.shared.showToast("Failed to format device MAC address.");
        return false;
      }
      bool activateResult = await XiaoZhiUtil.shared.agentsDevicesActivate(
        serialNumber,
        mac,
      );
      if (!activateResult) {
        debugPrint(" Device云Side / EndActivateFailed");
        if (mounted) {
          AppState.shared.showToast("Device cloud activation failed.");
        }
        return false;
      }

      ///deviceMayexist 但Notactivate,Needverify
      final checkDevice = await XiaoZhiUtil.shared.serialNumberGetDevice(
        serialNumber,
      );
      if (checkDevice == null || checkDevice.agent_id == null) {
        //activatefail
        return false;
      } else {
        AppState.shared.showToast("Device activation successful");
        debugPrint("DeviceActivateSuccess,DeviceId: ${checkDevice.device_id}");
        return true;
      }
    } catch (e) {
      debugPrint(" 查询DeviceConfigurationException: $e");
      if (mounted) {
        AppState.shared.showToast("Failed to query device configuration.");
      }
      return false;
    }
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
            largeTitle: Text("Select Device"),
            leading: SizedBox.shrink(),
            trailing: CupertinoButton(
              padding: .zero,
              child: Icon(
                CupertinoIcons.xmark_circle_fill,
                size: 25,
                color: CupertinoColors.separator.resolveFrom(context),
              ),
              onPressed: () {
                AppState.shared.manualShutdownTime = DateTime.now();
                BlueUtil.shared.characteristicCallback = null;
                BlueUtil.shared.wifiSetCharacteristicCall = null;
                BlueUtil.shared.onReconnectSuccess = null;
                CupertinoSheetRoute.popSheet(context);
              },
            ),
          ),
          SliverList.list(
            children: [
              Obx(
                () => CupertinoListSection.insetGrouped(
                  children: AppState.shared.blueDeviceList.isNotEmpty
                      ? AppState.shared.blueDeviceList.map((deviceInfo) {
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

                              //Repeatclick:Alreadyinconnectin
                              final deviceRemoteId = deviceInfo.device.remoteId
                                  .toString();
                              if (deviceRemoteId == connectDeviceId.value) {
                                if (mounted) {
                                  AppState.shared.showToast(
                                    "Already connecting... Please wait patiently.",
                                  );
                                }
                                return;
                              }

                              //startconnect
                              isSuccess = false;
                              connectDeviceId.value = deviceRemoteId;
                              AppState.shared.blueDeviceList.refresh();

                              //newIncrease / Add:connecttimeouttimer
                              _connectTimer?.cancel();
                              _connectTimer = Timer(
                                Duration(seconds: _connectTimeout),
                                () {
                                  _resetConnectState();
                                  debugPrint(" DeviceConnectTimeout");
                                  if (mounted) {
                                    AppState.shared.showToast(
                                      "Device connection timed out. Please try again.",
                                    );
                                  }
                                },
                              );

                              //newIncrease / Add:BluetoothconnectThrowsCatch
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
                                    "Bluetooth connection failed: ${e.toString()}",
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
                      : [CupertinoListTile(title: Text("No devices found"))],
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }

  //startverifydevice(send握手data)
  void startVerificationEquipment(
    BluetoothCharacteristic characteristic,
  ) async {
    try {
      if (mounted) {
        AppState.shared.showToast("Verifying device...");
      }
      //startverifytimeouttimer
      _verifyTimer?.cancel();
      _verifyTimer = Timer(Duration(seconds: _verifyTimeout), () {
        _resetConnectState();
        debugPrint(" DeviceVerifyTimeout");
        if (mounted) {
          AppState.shared.showToast(
            "Device verification timed out. Please try again.",
          );
        }
      });
      //构建握手data
      final dateTimeString = DateTime.now().millisecondsSinceEpoch.toString();
      final BlueEncryptionDecryption data = BlueEncryptionDecryption(
        cmd: "handshake",
        data: dateTimeString,
      );
      final jsonString = jsonEncode(data.toJson());

      //senddata
      bool result = await BlueUtil.shared.sendWifiSetData(jsonString);
      if (result) {
        debugPrint(" 发送DeviceVerify指令Success");
      } else {
        _verifyTimer?.cancel();
        AppState.shared.showToast(
          "The equipment may have been disconnected. Please reconfigure it on the StackChan end.",
        );
        _resetConnectState();
      }
    } catch (e) {
      _verifyTimer?.cancel();
      _resetConnectState();
      debugPrint(" 发送DeviceVerify指令Failed: $e");
      if (mounted) {
        AppState.shared.showToast(
          "Failed to send device verification command.",
        );
      }
    }
  }
}

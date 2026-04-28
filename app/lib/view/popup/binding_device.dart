/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:convert';

import 'package:flutter/cupertino.dart';
import 'package:flutter_svg/svg.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/util/blue_util.dart';
import 'package:stack_chan/util/value_constant.dart';
import 'package:stack_chan/view/popup/select_blue_device.dart';
import 'package:stack_chan/view/util/loop_playback_video.dart';
import 'package:stack_chan/view/util/scan_view.dart';

class BindingDevice extends StatefulWidget {
  const BindingDevice({super.key});

  @override
  State<StatefulWidget> createState() => _BindingDeviceState();
}

class _BindingDeviceState extends State<BindingDevice> {
  @override
  void initState() {
    super.initState();
    openBlue();
  }

  @override
  void dispose() {
    BlueUtil.shared.blufDevicesMonitoring = null;
    super.dispose();
  }

  void openBlue() {
    BlueUtil.shared.blufDevicesMonitoring = (devices) {
      final pairingModeDevices = AppState.shared.screeningDevices(devices);
      AppState.shared.blueDeviceList.value = pairingModeDevices;
      if (AppState.shared.blueDeviceList.isNotEmpty) {
        if (AppState.shared.manualShutdownTime != null) {
          final timeInterval = DateTime.now()
              .difference(AppState.shared.manualShutdownTime!)
              .inSeconds;
          if (timeInterval < 5) {
            return;
          }
        }
        if (AppState.shared.showBlueDevicesSetStep) {
          return;
        }
        AppState.shared.showBlueDevicesSetStep = true;
        Navigator.pushAndRemoveUntil(
          context,
          CupertinoPageRoute(
            builder: (context) {
              return SelectBlueDevice();
            },
          ),
          (route) => false,
        );
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
            largeTitle: Text("Add a new StackChan"),
            trailing: CupertinoButton(
              padding: EdgeInsets.zero,
              child: Icon(
                CupertinoIcons.xmark_circle_fill,
                size: 25,
                color: CupertinoColors.separator.resolveFrom(context),
              ),
              onPressed: () {
                CupertinoSheetRoute.popSheet(context);
              },
            ),
          ),

          SliverPadding(
            padding: .all(15),
            sliver: SliverList.list(
              children: [
                Image.asset(
                  "assets/lateral_image.png",
                  width: 150,
                  height: 150,
                ),
                SizedBox(height: 20),
                Row(
                  spacing: 15,
                  children: [
                    SvgPicture.asset(
                      "assets/1.circle.fill.svg",
                      width: 15,
                      height: 15,
                      colorFilter: ColorFilter.mode(
                        CupertinoTheme.of(context).primaryColor,
                        BlendMode.srcIn,
                      ),
                    ),
                    Expanded(
                      child: Text("Put the new StackChan into binding mode"),
                    ),
                  ],
                ),
                SizedBox(height: 20),
                Row(
                  spacing: 15,
                  children: [
                    SvgPicture.asset(
                      "assets/2.circle.fill.svg",
                      width: 15,
                      height: 15,
                      colorFilter: ColorFilter.mode(
                        CupertinoTheme.of(context).primaryColor,
                        BlendMode.srcIn,
                      ),
                    ),
                    Expanded(
                      child: Column(
                        mainAxisSize: .min,
                        spacing: 5,
                        crossAxisAlignment: .start,
                        children: [
                          Text(
                            "If the welcome screen is displayed, tap \"Next\"",
                          ),
                          SizedBox(
                            width: 200,
                            child: Center(
                              child: ClipRSuperellipse(
                                borderRadius: .circular(20),
                                child: LoopPlaybackVideo(
                                  url: "assets/setup2.mov",
                                ),
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
                SizedBox(height: 20),
                Row(
                  spacing: 15,
                  children: [
                    SvgPicture.asset(
                      "assets/3.circle.fill.svg",
                      width: 15,
                      height: 15,
                      colorFilter: ColorFilter.mode(
                        CupertinoTheme.of(context).primaryColor,
                        BlendMode.srcIn,
                      ),
                    ),
                    Expanded(
                      child: Column(
                        mainAxisSize: .min,
                        spacing: 5,
                        crossAxisAlignment: .start,
                        children: [
                          Text(
                            "Otherwise, go to \"SETUP\" and tap \"Change Wi-Fi\"",
                          ),
                          SizedBox(
                            width: 200,
                            child: Center(
                              child: ClipRSuperellipse(
                                borderRadius: .circular(20),
                                child: LoopPlaybackVideo(
                                  url: "assets/setup1.mov",
                                ),
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
                SizedBox(height: 20),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class ScanningEquipment extends StatefulWidget {
  const ScanningEquipment({super.key});

  @override
  State<StatefulWidget> createState() => _ScanningEquipmentState();
}

class _ScanningEquipmentState extends State<ScanningEquipment> {
  @override
  Widget build(BuildContext context) {
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemBackground.resolveFrom(context),
      navigationBar: CupertinoNavigationBar.large(
        backgroundColor: CupertinoColors.systemBackground.resolveFrom(context),
        padding: .zero,
        leading: CupertinoNavigationBarBackButton(
          color: CupertinoTheme.of(context).primaryColor,
          previousPageTitle: "Back",
        ),
        largeTitle: Text("Scan QR Code"),
      ),
      child: Padding(
        padding: .only(
          top: 15 + MediaQuery.viewPaddingOf(context).top,
          bottom: 15 + MediaQuery.viewPaddingOf(context).bottom,
          left: 15,
          right: 15,
        ),
        child: ClipRSuperellipse(
          clipBehavior: .antiAliasWithSaveLayer,
          borderRadius: .circular(50),
          child: Container(
            color: CupertinoColors.black,
            width: .infinity,
            height: .infinity,
            child: ScanView(
              onDetect: (result) {
                if (result.barcodes.first.rawValue != null) {
                  readCodeString(result.barcodes.first.rawValue!);
                }
              },
            ),
          ),
        ),
      ),
    );
  }

  void readCodeString(String value) {
    try {
      final dynamic jsonData = jsonDecode(value);
      if (jsonData is Map &&
          jsonData.containsKey(ValueConstant.mac) &&
          jsonData[ValueConstant.mac] is String) {
        String mac = jsonData[ValueConstant.mac] as String;
        final RegExp macRegex = RegExp(r'[^A-F0-9]', caseSensitive: false);
        String cleanedMac = mac.toUpperCase().replaceAll(macRegex, '');
        AppState.shared.deviceMac = cleanedMac;
        AppState.shared.connectWebSocket();
        CupertinoSheetRoute.popSheet(context);
      }
    } on FormatException catch (e) {
          } on Exception catch (e) {
          }
  }
}

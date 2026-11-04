/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/native/native_bluetooth_bridge.dart';
import 'package:flutter_svg/svg.dart';
import 'package:liquid_glass_widgets/liquid_glass_widgets.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/util/blue_util.dart';
import 'package:stack_chan/view/popup/select_blue_device.dart';

import '../../model/blue_device_info.dart';
import '../util/theme_setting.dart';

class BindingDevice extends StatefulWidget {
  const BindingDevice({super.key});

  @override
  State<StatefulWidget> createState() => _BindingDeviceState();
}

class _BindingDeviceState extends State<BindingDevice> {
  NativeBluetoothAdapterState _adapterState =
      NativeBluetoothBridge.shared.adapterStateNow;
  Timer? _manualShutdownRetryTimer;

  @override
  void initState() {
    super.initState();
    BlueUtil.shared.centralManagerDidUpdateState = (state) {
      if (!mounted) {
        return;
      }
      setState(() {
        _adapterState = state;
      });
    };
    openBlue();
  }

  @override
  void dispose() {
    _manualShutdownRetryTimer?.cancel();
    if (!AppState.shared.showBlueDevicesSetStep) {
      BlueUtil.shared.blufDevicesMonitoring = null;
    }
    BlueUtil.shared.centralManagerDidUpdateState = null;
    super.dispose();
  }

  Future<void> _openBluetoothSettings() async {
    await openAppSettings();
  }

  void _evaluateDiscoveredDevices(List<BlueDeviceInfo> devices) {
    final pairingModeDevices = AppState.shared.screeningDevices(devices);
    AppState.shared.blueDeviceList.value = pairingModeDevices;
    if (AppState.shared.blueDeviceList.isEmpty) {
      return;
    }
    if (AppState.shared.manualShutdownTime != null) {
      final timeInterval = DateTime.now()
          .difference(AppState.shared.manualShutdownTime!)
          .inSeconds;
      if (timeInterval < 5) {
        final waitSeconds = 5 - timeInterval;
        _manualShutdownRetryTimer?.cancel();
        _manualShutdownRetryTimer = Timer(Duration(seconds: waitSeconds), () {
          if (!mounted || AppState.shared.showBlueDevicesSetStep) {
            return;
          }
          _evaluateDiscoveredDevices(BlueUtil.shared.discoveredDevices);
        });
        return;
      }
    }
    if (AppState.shared.showBlueDevicesSetStep) {
      return;
    }
    AppState.shared.showBlueDevicesSetStep = true;
    // Replace only this route. Using pushAndRemoveUntil here would clear the
    // root navigator (and with it the home page) when the binding popup is
    // presented as a dialog on wide screens.
    Navigator.pushReplacement(
      context,
      CupertinoPageRoute(
        builder: (context) {
          return const SelectBlueDevice();
        },
      ),
    );
  }

  void openBlue() {
    BlueUtil.shared.blufDevicesMonitoring = (devices) {
      if (!mounted) {
        return;
      }
      _evaluateDiscoveredDevices(devices);
    };

    _evaluateDiscoveredDevices(BlueUtil.shared.discoveredDevices);
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
            stretch: true,
            largeTitle: const Text("Add a new StackChan"),
            leading: const SizedBox.shrink(),
            trailing: CupertinoButton(
              sizeStyle: .medium,
              onPressed: () {
                Navigator.maybePop(context);
              },
              child: const Icon(CupertinoIcons.xmark),
            ),
          ),

          SliverPadding(
            padding: .only(
              top: 20,
              left: padding.left + 20,
              right: padding.right + 20,
              bottom: padding.bottom + 20,
            ),
            sliver: SliverList.list(
              children: [
                Image.asset(
                  "assets/lateral_image.png",
                  width: 150,
                  height: 150,
                ),
                const SizedBox(height: 20),

                if (_adapterState != NativeBluetoothAdapterState.on) ...[
                  SizedBox(
                    width: double.infinity,
                    child: GlassButton.custom(
                      height: 50,
                      settings: LiquidGlassSettings(
                        glassColor: CupertinoColors.destructiveRed.resolveFrom(
                          context,
                        ),
                      ),
                      shape: const LiquidRoundedSuperellipse(borderRadius: 23),
                      onTap: _openBluetoothSettings,
                      child: const Center(
                        child: Text(
                          "The Bluetooth on your mobile phone is not turned on.",
                          style: TextStyle(color: CupertinoColors.white),
                          textAlign: TextAlign.center,
                        ),
                      ),
                    ),
                  ),
                  const SizedBox(height: 20),
                ],

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
                    const Expanded(
                      child: Text("Put the new StackChan into binding mode"),
                    ),
                  ],
                ),
                const SizedBox(height: 20),
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
                          const Text(
                            "If the welcome screen is displayed, tap \"Next\"",
                          ),
                          SizedBox(
                            width: 200,
                            child: Center(
                              child: ClipRSuperellipse(
                                borderRadius: .circular(12),
                                child: Image.asset("assets/setup2.gif"),
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 20),
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
                          const Text(
                            "Otherwise, go to \"SETUP\" and tap \"Change Wi-Fi\"",
                          ),
                          SizedBox(
                            width: 200,
                            child: Center(
                              child: ClipRSuperellipse(
                                borderRadius: .circular(12),
                                child: Image.asset("assets/setup1.gif"),
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 20),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

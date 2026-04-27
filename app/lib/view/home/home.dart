/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';

import 'package:flutter/cupertino.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:stack_chan/util/blue_util.dart';
import 'package:stack_chan/view/home/settings.dart';
import 'package:stack_chan/view/home/stack_chan.dart';

import '../../app_state.dart';
import '../../model/blue_device_info.dart';

class Home extends StatefulWidget {
  const Home({super.key});

  @override
  State<StatefulWidget> createState() => _HomeState();
}

class _HomeState extends State<Home> {
  @override
  void initState() {
    super.initState();
    init();
  }

  Future<void> init() async {
    //Main页initwhensetasWiFimode
    BlueUtil.shared.blueMode = 1;

    AppState.shared.webSocketMessageMonitoring();
    if (AppState.shared.deviceMac != "") {
      AppState.shared.connectWebSocket();
    }

    //setBluetoothdevicescancallback,updateMain页devicelist
    BlueUtil.shared.blufDevicesMonitoring = (List<BlueDeviceInfo> devices) {
      AppState.shared.blueDeviceList.value = devices;
    };
    //Delaytriggerscan,Ensure BlueUtil SingletonAlreadyfullyinit
    Timer.run(() {
      if (BlueUtil.shared.blueSwitch && BlueUtil.shared.automaticScanning) {
        BlueUtil.shared.startScan();
      }
    });
  }

  int pageIndex = 0;

  @override
  void dispose() {
    //removeBluetoothscancallback
    BlueUtil.shared.blufDevicesMonitoring = null;
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    Color activeColor = CupertinoTheme.of(context).primaryColor;
    Color inactiveColor = CupertinoColors.inactiveGray.resolveFrom(context);
    double size = 20;
    return CupertinoTabScaffold(
      tabBar: CupertinoTabBar(
        backgroundColor: CupertinoTheme.of(
          context,
        ).barBackgroundColor.withValues(alpha: 0.6),
        currentIndex: 0,
        onTap: (index) {
          setState(() {
            pageIndex = index;
          });
        },
        items: [
          BottomNavigationBarItem(
            icon: SvgPicture.asset(
              "assets/ipod.svg",
              colorFilter: .mode(
                pageIndex == 0 ? activeColor : inactiveColor,
                .srcIn,
              ),
              width: size,
              height: size,
            ),
            label: "StackChan",
          ),
          // BottomNavigationBarItem(
          //   icon: SvgPicture.asset(
          //     "assets/sensor.svg",
          //     colorFilter: .mode(
          //       pageIndex == 1 ? activeColor : inactiveColor,
          //       .srcIn,
          //     ),
          //     width: size,
          //     height: size,
          //   ),
          //   label: "Nearby",
          // ),
          // BottomNavigationBarItem(
          //   icon: SvgPicture.asset(
          //     "assets/person.3.svg",
          //     colorFilter: .mode(
          //       pageIndex == 2 ? activeColor : inactiveColor,
          //       .srcIn,
          //     ),
          //     width: size,
          //     height: size,
          //   ),
          //   label: "Moments",
          // ),
          BottomNavigationBarItem(
            icon: SvgPicture.asset(
              "assets/gear.svg",
              colorFilter: .mode(
                pageIndex == 1 ? activeColor : inactiveColor,
                .srcIn,
              ),
              width: size,
              height: size,
            ),
            label: "Settings",
          ),
        ],
      ),
      tabBuilder: (BuildContext context, int index) {
        return CupertinoTabView(
          builder: (context) {
            switch (index) {
              case 0:
                return StackChan();
              // case 1:
              //   return Nearby();
              // case 2:
              //   return Moments();
              case 1:
                return Settings();
              default:
                return SizedBox();
            }
          },
        );
      },
    );
  }
}

/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:get/get.dart';
import 'package:pull_down_button/pull_down_button.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/view/app.dart';
import 'package:stack_chan/view/home/avatar.dart';
import 'package:stack_chan/view/home/monitoring_camera.dart';
import 'package:stack_chan/view/popup/motion.dart';

import '../../util/custom_colors.dart';
import '../util/gradation_page_background.dart';
import 'dance_list_page.dart';

class StackChan extends StatefulWidget {
  const StackChan({super.key});

  @override
  State<StatefulWidget> createState() => _StackChanState();
}

class _StackChanState extends State<StackChan> {
  RxString deviceStatus = "".obs;

  @override
  void initState() {
    super.initState();
    if (AppState.shared.isLogin.value) {
      AppState.shared.getDevices();
    }
  }

  //selectdevicemoreoption
  Future<void> showUnbindingPopup() async {
    showCupertinoModalPopup(
      context: context,
      builder: (context) {
        return CupertinoActionSheet(
          title: Text(AppState.shared.deviceInfo.value?.name ?? "StackChan"),
          cancelButton: CupertinoActionSheetAction(
            onPressed: () {
              Navigator.pop(context);
            },
            child: Text("Cancel"),
          ),
          actions: [
            CupertinoActionSheetAction(
              onPressed: () {
                Navigator.pop(context);
                showCupertinoDialog(
                  context: context,
                  builder: (context) {
                    return CupertinoAlertDialog(
                      title: const Text('Unbind Confirmation'),
                      content: const Text(
                        'Are you sure you want to unbind this device?',
                      ),
                      actions: [
                        CupertinoDialogAction(
                          child: const Text('Cancel'),
                          onPressed: () {
                            Navigator.pop(context);
                          },
                        ),
                        CupertinoDialogAction(
                          isDestructiveAction: true,
                          onPressed: () {
                            Navigator.pop(context);
                            AppState.shared.unbindDevice(
                              AppState.shared.deviceMac,
                            );
                          },
                          child: const Text('Unbind'),
                        ),
                      ],
                    );
                  },
                );
              },
              isDestructiveAction: true,
              child: Text("Unbind"),
            ),
          ],
        );
      },
    );
  }

  @override
  Widget build(BuildContext context) {
    double? textSize = CupertinoTheme.of(
      context,
    ).textTheme.navLargeTitleTextStyle.fontSize;

    return CupertinoPageScaffold(
      child: GradationPageBackground(
        child: CustomScrollView(
          slivers: [
            CupertinoSliverNavigationBar(
              brightness: MediaQuery.of(context).platformBrightness,
              automaticBackgroundVisibility: false,
              border: null,
              largeTitle: Text("StackChan World"),
              backgroundColor: CustomColors.transparent,
            ),
            SliverPadding(
              padding: .all(20),
              sliver: SliverList.list(
                children: [
                  Row(
                    children: [
                      Image.asset("assets/image1.png", width: 100, height: 100),
                      Spacer(),
                      Obx(() {
                        if (AppState.shared.devices.isEmpty) {
                          return CupertinoButton(
                            padding: .all(15),
                            minimumSize: .zero,
                            pressedOpacity: 1,
                            borderRadius: .circular(50),
                            color: CupertinoColors.secondarySystemFill
                                .resolveFrom(context),
                            onPressed: () {
                              AppState.shared.showBindingDevice(context);
                            },
                            child: Row(
                              mainAxisSize: .min,
                              spacing: 5,
                              children: [
                                Icon(CupertinoIcons.add_circled, size: 18),
                                ConstrainedBox(
                                  constraints: BoxConstraints(maxWidth: 180),
                                  child: Text(
                                    "Add a new StackChan",
                                    maxLines: 2,
                                    overflow: .visible,
                                    style: TextStyle(
                                      fontSize: 17,
                                      fontWeight: .bold,
                                    ),
                                  ),
                                ),
                              ],
                            ),
                          );
                        } else {
                          return PullDownButton(
                            itemBuilder: (context) {
                              final list = AppState.shared.devices
                                  .map(
                                    (value) => PullDownMenuItem.selectable(
                                      iconWidget: Image.asset(
                                        "assets/image1.png",
                                      ),
                                      selected:
                                          value.mac ==
                                          AppState.shared.deviceMac,
                                      onTap: () {
                                        if (value.mac !=
                                            AppState
                                                .shared
                                                .deviceInfo
                                                .value
                                                ?.mac) {
                                          AppState.shared.switchDevice(value);
                                        }
                                      },
                                      title: value.getDisplayName(),
                                    ),
                                  )
                                  .toList();

                              list.add(
                                PullDownMenuItem(
                                  icon: CupertinoIcons.add_circled,
                                  onTap: () => AppState.shared
                                      .showBindingDevice(context),
                                  title: "Add a new StackChan",
                                ),
                              );
                              return list;
                            },
                            buttonBuilder: (context, showMenu) =>
                                CupertinoButton(
                                  padding: .all(15),
                                  minimumSize: .zero,
                                  onLongPress: () {
                                    showUnbindingPopup();
                                  },
                                  pressedOpacity: 1,
                                  borderRadius: .circular(50),
                                  color: CupertinoColors.secondarySystemFill
                                      .resolveFrom(context),
                                  onPressed: showMenu,
                                  child: Row(
                                    mainAxisSize: .min,
                                    spacing: 5,
                                    children: [
                                      if (AppState.shared.devices.length > 1)
                                        Icon(
                                          CupertinoIcons.chevron_down,
                                          size: 25,
                                        ),
                                      ConstrainedBox(
                                        constraints: BoxConstraints(
                                          maxWidth: 150,
                                        ),
                                        child: Text(
                                          AppState.shared.deviceInfo.value
                                                  ?.getDisplayName() ??
                                              "",
                                          style: TextStyle(
                                            fontSize: 18,
                                            fontWeight: .bold,
                                          ),
                                        ),
                                      ),
                                    ],
                                  ),
                                ),
                          );
                        }
                      }),
                    ],
                  ),
                  SizedBox(height: 20),
                  CupertinoButton(
                    padding: .zero,
                    child: ClipRSuperellipse(
                      clipBehavior: .antiAliasWithSaveLayer,
                      borderRadius: .circular(35),
                      child: Container(
                        color: CustomColors.ff659c,
                        padding: .only(
                          left: 20,
                          right: 20,
                          top: 20,
                          bottom: 20,
                        ),
                        child: Row(
                          children: [
                            SizedBox(
                              width: 44,
                              height: 44,
                              child: Image.asset(
                                "assets/avatar_icon.png",
                                width: 44,
                                height: 44,
                              ),
                            ),
                            Spacer(),
                            Text(
                              "AVATAR",
                              textScaler: .noScaling,
                              style: TextStyle(
                                fontSize: textSize,
                                fontWeight: .bold,
                                color: CupertinoColors.systemBackground
                                    .resolveFrom(context),
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                    onPressed: () {
                      if (AppState.shared.deviceMac.isEmpty) {
                        //showbindpopup
                        AppState.shared.showBindingDevice(context);
                      } else {
                        Navigator.of(context, rootNavigator: true).push(
                          CupertinoPageRoute(
                            builder: (context) {
                              return Avatar(
                                deviceMac: AppState.shared.deviceMac,
                              );
                            },
                          ),
                        );
                      }
                    },
                  ),
                  SizedBox(height: 20),
                  CupertinoButton(
                    padding: .zero,
                    child: ClipRSuperellipse(
                      clipBehavior: .antiAliasWithSaveLayer,
                      borderRadius: .circular(35),
                      child: Container(
                        color: CupertinoColors.label
                            .resolveFrom(context)
                            .withValues(alpha: 0.8),
                        padding: .only(
                          left: 20,
                          right: 20,
                          top: 20,
                          bottom: 20,
                        ),
                        child: Row(
                          children: [
                            SizedBox(
                              width: 44,
                              height: 44,
                              child: SvgPicture.asset(
                                "assets/video.svg",
                                colorFilter: .mode(
                                  CupertinoColors.systemBackground.resolveFrom(
                                    context,
                                  ),
                                  .srcIn,
                                ),
                                width: 44,
                                height: 44,
                              ),
                            ),
                            Spacer(),
                            Text(
                              "MONITORING\nCAMERA",
                              textAlign: .end,
                              textScaler: .noScaling,
                              style: TextStyle(
                                fontSize: textSize,
                                fontWeight: .bold,
                                color: CupertinoColors.systemBackground
                                    .resolveFrom(context),
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                    onPressed: () {
                      if (AppState.shared.deviceMac.isEmpty) {
                        //showbindpopup
                        AppState.shared.showBindingDevice(context);
                      } else {
                        Navigator.of(context, rootNavigator: true).push(
                          CupertinoPageRoute(
                            builder: (context) {
                              return MonitoringCamera();
                            },
                          ),
                        );
                      }
                    },
                  ),
                  SizedBox(height: 20),
                  CupertinoButton(
                    padding: .zero,
                    child: ClipRSuperellipse(
                      borderRadius: .circular(35),
                      clipBehavior: .antiAliasWithSaveLayer,
                      child: Container(
                        color: CupertinoColors.inactiveGray
                            .resolveFrom(context)
                            .withValues(alpha: 0.5),
                        padding: .only(
                          left: 20,
                          right: 20,
                          top: 20,
                          bottom: 20,
                        ),
                        child: Row(
                          children: [
                            SizedBox(
                              width: 44,
                              height: 44,
                              child: SvgPicture.asset(
                                "assets/arrow.up.and.down.and.arrow.left.and.right.svg",
                                colorFilter: .mode(
                                  CupertinoColors.label.resolveFrom(context),
                                  .srcIn,
                                ),
                                width: 44,
                                height: 44,
                              ),
                            ),
                            Spacer(),
                            Text(
                              "MOTION",
                              textScaler: .noScaling,
                              style: TextStyle(
                                fontSize: textSize,
                                fontWeight: .bold,
                                color: CupertinoColors.label.resolveFrom(
                                  context,
                                ),
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                    onPressed: () {
                      if (AppState.shared.deviceMac.isEmpty) {
                        //showbindpopup
                        AppState.shared.showBindingDevice(context);
                      } else {
                        App.showAppSheet(showDragHandle: true, (context) {
                          return Motion();
                        });
                      }
                    },
                  ),
                  SizedBox(height: 20),
                  CupertinoButton(
                    padding: .zero,
                    child: ClipRSuperellipse(
                      borderRadius: .circular(35),
                      clipBehavior: .antiAliasWithSaveLayer,
                      child: Container(
                        color: CupertinoColors.activeOrange.resolveFrom(
                          context,
                        ),
                        padding: .only(
                          left: 20,
                          right: 20,
                          top: 20,
                          bottom: 20,
                        ),
                        child: Row(
                          children: [
                            SizedBox(
                              width: 44,
                              height: 44,
                              child: SvgPicture.asset(
                                "assets/figure.dance.svg",
                                colorFilter: .mode(
                                  CupertinoColors.systemBackground.resolveFrom(
                                    context,
                                  ),
                                  .srcIn,
                                ),
                                width: 44,
                                height: 44,
                              ),
                            ),
                            Spacer(),
                            Text(
                              "DANCE",
                              textScaler: .noScaling,
                              style: TextStyle(
                                fontSize: textSize,
                                fontWeight: .bold,
                                color: CupertinoColors.systemBackground
                                    .resolveFrom(context),
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                    onPressed: () {
                      if (AppState.shared.deviceMac.isEmpty) {
                        //showbindpopup
                        AppState.shared.showBindingDevice(context);
                      } else {
                        Navigator.of(context, rootNavigator: true).push(
                          CupertinoPageRoute(
                            builder: (context) {
                              return DanceListPage();
                            },
                          ),
                        );
                      }
                    },
                  ),
                  SizedBox(height: 20),
                  // CupertinoButton(
                  //   padding: .zero,
                  //   child: ClipRSuperellipse(
                  //     borderRadius: .circular(35),
                  //     clipBehavior: .antiAliasWithSaveLayer,
                  //     child: Container(
                  //       color: CupertinoColors.systemBlue.resolveFrom(context),
                  //       padding: .only(
                  //         left: 20,
                  //         right: 20,
                  //         top: 20,
                  //         bottom: 20,
                  //       ),
                  //       child: Row(
                  //         children: [
                  //           SizedBox(
                  //             width: 44,
                  //             height: 44,
                  //             child: SvgPicture.asset(
                  //               "assets/pano.svg",
                  //               colorFilter: .mode(
                  //                 CupertinoColors.systemBackground.resolveFrom(
                  //                   context,
                  //                 ),
                  //                 .srcIn,
                  //               ),
                  //               width: 44,
                  //               height: 44,
                  //             ),
                  //           ),
                  //           Spacer(),
                  //           Text(
                  //             "PANO",
                  //             textScaler: .noScaling,
                  //             style: TextStyle(
                  //               fontSize: textSize,
                  //               fontWeight: .bold,
                  //               color: CupertinoColors.systemBackground
                  //                   .resolveFrom(context),
                  //             ),
                  //           ),
                  //         ],
                  //       ),
                  //     ),
                  //   ),
                  //   onPressed: () {
                  //     // if (AppState.shared.deviceMac.isEmpty) {
                  ////   // showbindpopup
                  //     //   AppState.shared.showBindingDevice(context);
                  //     // } else {
                  //     //   Navigator.of(context, rootNavigator: true).push(
                  //     //     CupertinoPageRoute(
                  //     //       builder: (context) {
                  //     //         return PanoPage();
                  //     //       },
                  //     //     ),
                  //     //   );
                  //     // }
                  //   },
                  // ),
                  SizedBox(height: 20 + MediaQuery.of(context).padding.bottom),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

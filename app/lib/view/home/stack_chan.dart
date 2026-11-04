/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/popup.dart';
import 'package:stack_chan/view/util/theme_setting.dart';
import 'package:flutter/services.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:get/get.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/util/debug_log_buffer.dart';
import 'package:stack_chan/view/home/avatar.dart';
import 'package:stack_chan/view/home/monitoring_camera.dart';
import 'package:stack_chan/view/popup/motion.dart';

import '../../util/custom_colors.dart';
import '../util/gradation_page_background.dart';
import 'dance_list_page.dart';

class StackChan extends StatefulWidget {
  const StackChan({super.key, required this.bottomSafeArea});

  final double bottomSafeArea;

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

  // Show the device actions sheet.
  Future<void> showUnbindingPopup() async {
    showCupertinoModalPopup(
      context: context,
      builder: (context) {
        return CupertinoActionSheet(
          title: Text(AppState.shared.deviceInfo.value?.name ?? "StackChan"),
          cancelButton: CupertinoActionSheetAction(
            onPressed: () {
              Navigator.maybePop(context);
            },
            child: const Text("Cancel"),
          ),
          actions: [
            CupertinoActionSheetAction(
              onPressed: () {
                Navigator.maybePop(context);
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
                            Navigator.maybePop(context);
                          },
                        ),
                        CupertinoDialogAction(
                          isDestructiveAction: true,
                          onPressed: () {
                            Navigator.maybePop(context);
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
              child: const Text("Unbind"),
            ),
          ],
        );
      },
    );
  }

  Future<void> _copyCurrentLogs() async {
    final logs = DebugLogBuffer.text;
    if (logs.isEmpty) {
      AppState.shared.showToast("No logs to copy");
      return;
    }

    await Clipboard.setData(ClipboardData(text: logs));
    AppState.shared.showToast("Logs copied");
  }

  @override
  Widget build(BuildContext context) {
    double? textSize = CupertinoTheme.of(context)
        .textTheme
        .navLargeTitleTextStyle
        .fontSize;

    final screenPadding = ThemeSetting.screenPadding(context);

    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      child: GradationPageBackground(
        child: CustomScrollView(
          slivers: [
            CupertinoSliverNavigationBar(
              backgroundColor: CupertinoColors.systemGroupedBackground
                  .resolveFrom(context)
                  .withValues(alpha: 0.01),
              border: null,
              padding: ThemeSetting.navigationBarPadding,
              stretch: true,
              brightness: CupertinoTheme.brightnessOf(context),
              automaticBackgroundVisibility: false,
              largeTitle: const Text("StackChan World"),
            ),
            SliverPadding(
              padding: .only(
                top: 20,
                bottom: 20 + widget.bottomSafeArea,
                left: screenPadding.left + 20,
                right: screenPadding.right + 20,
              ),
              sliver: SliverList.list(
                children: [
                  Row(
                    children: [
                      Image.asset("assets/image1.png", width: 100, height: 100),
                      const Spacer(),
                      Obx(() {
                        if (AppState.shared.devices.isEmpty) {
                          return CupertinoButton(
                            color: CupertinoColors.secondarySystemFill
                                .resolveFrom(context),
                            pressedOpacity: 1,
                            borderRadius: .circular(
                              ThemeSetting.mediumCardRadius,
                            ),
                            onPressed: () {
                              AppState.shared.showBindingDevice(context);
                            },
                            child: Row(
                              mainAxisSize: .min,
                              spacing: 5,
                              children: [
                                const Icon(CupertinoIcons.add, size: 18),
                                ConstrainedBox(
                                  constraints: const BoxConstraints(
                                    maxWidth: 180,
                                  ),
                                  child: const Text(
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
                          final list = AppState.shared.devices
                              .map(
                                (value) => CupertinoMenuItem(
                                  leading: Image.asset(
                                    "assets/image1.png",
                                    width: 20,
                                    height: 20,
                                  ),
                                  trailing:
                                      value.mac == AppState.shared.deviceMac
                                      ? const Icon(CupertinoIcons.check_mark)
                                      : null,
                                  onPressed: () {
                                    if (value.mac !=
                                        AppState.shared.deviceInfo.value?.mac) {
                                      AppState.shared.switchDevice(value);
                                    }
                                  },
                                  child: Text(value.getDisplayName()),
                                ),
                              )
                              .toList();
                          list.add(
                            CupertinoMenuItem(
                              leading: const Icon(CupertinoIcons.add),
                              onPressed: () {
                                AppState.shared.showBindingDevice(context);
                              },
                              child: const Text("Add a new StackChan"),
                            ),
                          );
                          return CupertinoMenuAnchor(
                            constraints: const BoxConstraints.tightFor(
                              width: 240,
                            ),
                            consumeOutsideTaps: true,
                            overlayPadding: MediaQuery.viewPaddingOf(context),
                            menuChildren: list,
                            builder: (context, controller, child) {
                              return CupertinoButton(
                                color: CupertinoColors.secondarySystemFill
                                    .resolveFrom(context),
                                onLongPress: () {
                                  showUnbindingPopup();
                                },
                                pressedOpacity: 1,
                                onPressed: () {
                                  if (controller.isOpen) {
                                    controller.close();
                                  } else {
                                    controller.open();
                                  }
                                },
                                borderRadius: .circular(
                                  ThemeSetting.mediumCardRadius,
                                ),
                                child: Row(
                                  mainAxisSize: .min,
                                  spacing: 5,
                                  children: [
                                    if (AppState.shared.devices.length > 1)
                                      const Icon(
                                        CupertinoIcons.chevron_down,
                                        size: 25,
                                      ),
                                    ConstrainedBox(
                                      constraints: const BoxConstraints(
                                        maxWidth: 150,
                                      ),
                                      child: Text(
                                        AppState.shared.deviceInfo.value
                                                ?.getDisplayName() ??
                                            "",
                                        style: const TextStyle(
                                          fontSize: 18,
                                          fontWeight: .bold,
                                        ),
                                      ),
                                    ),
                                  ],
                                ),
                              );
                            },
                          );
                        }
                      }),
                    ],
                  ),
                  const SizedBox(height: 20),

                  // if (kDebugMode) ...[
                  //   CupertinoButton.filled(
                  //     borderRadius: .circular(ThemeSetting.mediumCardRadius),
                  //     onPressed: _copyCurrentLogs,
                  //     child: const Text("out lops"),
                  //   ),
                  //   const SizedBox(height: 20),
                  // ],
                  CupertinoButton(
                    padding: const .all(20),
                    borderRadius: .circular(ThemeSetting.mediumCardRadius),
                    color: CustomColors.ff659c,
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
                        const Spacer(),
                        Text(
                          "AVATAR",
                          textScaler: .noScaling,
                          style: TextStyle(
                            fontSize: textSize,
                            fontWeight: .bold,
                            color: CupertinoColors.systemBackground.resolveFrom(
                              context,
                            ),
                          ),
                        ),
                      ],
                    ),
                    onPressed: () {
                      if (AppState.shared.deviceMac.isEmpty) {
                        // Prompt the user to bind a device first.
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
                  const SizedBox(height: 20),
                  CupertinoButton(
                    borderRadius: .circular(ThemeSetting.mediumCardRadius),
                    color: CupertinoColors.label
                        .resolveFrom(context)
                        .withValues(alpha: 0.8),
                    padding: const .all(20),
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
                        const Spacer(),
                        Text(
                          "CAMERA",
                          textAlign: .end,
                          textScaler: .noScaling,
                          style: TextStyle(
                            fontSize: textSize,
                            fontWeight: .bold,
                            color: CupertinoColors.systemBackground.resolveFrom(
                              context,
                            ),
                          ),
                        ),
                      ],
                    ),
                    onPressed: () {
                      if (AppState.shared.deviceMac.isEmpty) {
                        // Prompt the user to bind a device first.
                        AppState.shared.showBindingDevice(context);
                      } else {
                        Navigator.of(context, rootNavigator: true).push(
                          CupertinoPageRoute(
                            builder: (context) {
                              return const MonitoringCamera();
                            },
                          ),
                        );
                      }
                    },
                  ),
                  const SizedBox(height: 20),
                  CupertinoButton(
                    borderRadius: .circular(ThemeSetting.mediumCardRadius),
                    color: CupertinoColors.inactiveGray
                        .resolveFrom(context)
                        .withValues(alpha: 0.5),
                    padding: const .all(20),
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
                        const Spacer(),
                        Text(
                          "MOTION",
                          textScaler: .noScaling,
                          style: TextStyle(
                            fontSize: textSize,
                            fontWeight: .bold,
                            color: CupertinoColors.label.resolveFrom(context),
                          ),
                        ),
                      ],
                    ),
                    onPressed: () {
                      if (AppState.shared.deviceMac.isEmpty) {
                        // Prompt the user to bind a device first.
                        AppState.shared.showBindingDevice(context);
                      } else {
                        showCustomCupertinoSheet(
                          context: context,
                          scrollableBuilder: (context, scrollController) {
                            return Motion(scrollController: scrollController);
                          },
                        );
                      }
                    },
                  ),
                  const SizedBox(height: 20),
                  CupertinoButton(
                    borderRadius: .circular(ThemeSetting.mediumCardRadius),
                    padding: const .all(20),
                    color: const Color(0xFFB77BFF),
                    child: Row(
                      children: [
                        SizedBox(
                          width: 44,
                          height: 44,
                          child: Image.asset("assets/dance_stackchan.png"),
                        ),
                        const Spacer(),
                        Text(
                          "DANCE",
                          textScaler: .noScaling,
                          style: TextStyle(
                            fontSize: textSize,
                            fontWeight: .bold,
                            color: CupertinoColors.systemBackground.resolveFrom(
                              context,
                            ),
                          ),
                        ),
                      ],
                    ),
                    onPressed: () {
                      if (AppState.shared.deviceMac.isEmpty) {
                        // Prompt the user to bind a device first.
                        AppState.shared.showBindingDevice(context);
                      } else {
                        Navigator.push(
                          context,
                          CupertinoPageRoute(
                            builder: (context) {
                              return const DanceListPage();
                            },
                          ),
                        );
                      }
                    },
                  ),
                  const SizedBox(height: 20),
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
                  //     if (AppState.shared.deviceMac.isEmpty) {
                  //       // showBindPopup
                  //       AppState.shared.showBindingDevice(context);
                  //     } else {
                  //       Navigator.of(context, rootNavigator: true).push(
                  //         CupertinoPageRoute(
                  //           builder: (context) {
                  //             return PanoPage();
                  //           },
                  //         ),
                  //       );
                  //     }
                  //   },
                  // ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

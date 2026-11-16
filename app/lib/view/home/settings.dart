/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:io';

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/theme_setting.dart';
import 'package:flutter/services.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:get/get.dart';
import 'package:liquid_glass_widgets/liquid_glass_widgets.dart';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/view/home/conversation_page.dart';
import 'package:stack_chan/view/home/mcp_page.dart';
import 'package:stack_chan/view/popup/user_info_page.dart';
import 'package:url_launcher/url_launcher.dart';

import '../popup/xiaozhi_welcome_page.dart';
import '../util/popup.dart';

class Settings extends StatefulWidget {
  const Settings({super.key, required this.bottomSafeArea});

  final double bottomSafeArea;

  @override
  State<StatefulWidget> createState() => _SettingsState();
}

class _SettingsState extends State<Settings> {
  String appVersion = "";

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (AppState.shared.isLogin.value) {
        AppState.shared.getUserInfo();
        AppState.shared.getDeviceInfo();
      }
    });
    getPubspecInfo();
  }

  Future<void> getPubspecInfo() async {
    final packageInfo = await PackageInfo.fromPlatform();
    setState(() {
      appVersion = packageInfo.version;
    });
  }

  @override
  void dispose() {
    FocusManager.instance.primaryFocus?.unfocus();
    super.dispose();
  }

  void _navigateToPage(Widget page) {
    Navigator.of(
      context,
      rootNavigator: true,
    ).push(CupertinoPageRoute(builder: (context) => page));
  }

  bool _checkDeviceBinding() {
    if (AppState.shared.deviceMac.isEmpty) {
      AppState.shared.showBindingDevice(context);
      return false;
    }
    return true;
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

  @override
  Widget build(BuildContext context) {
    List<Widget> sectionList1 = [_buildChangeNameTile()];
    List<Widget> sectionList2 = [
      _buildAgentConfigTile(),
      _buildMcpListTile(),
      _buildConversationListTile(),
    ];
    List<Widget> sectionList3 = [_unbindResetTitle(), _buildBindDeviceTile()];
    List<Widget> sectionList4 = [
      CupertinoListTile(
        title: const Text("Version"),
        leading: _buildSectionIcon(
          iconPath: "assets/v.square.svg",
          color: CupertinoColors.black,
        ),
        onTap: _launchAppStore,
        trailing: Row(
          spacing: 10,
          mainAxisSize: .min,
          children: [Text("v$appVersion"), _buildChevronIcon()],
        ),
      ),
    ];

    final screenPadding = ThemeSetting.screenPadding(context);

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
            largeTitle: const Text("Settings"),
            trailing: CupertinoButton(
              sizeStyle: .medium,
              onPressed: () {
                if (AppState.shared.isLogin.value) {
                  showCustomCupertinoSheet(
                    context: context,
                    scrollableBuilder: (context, scrollController) =>
                        UserInfoPage(scrollController: scrollController),
                  );
                } else {
                  AppState.shared.showLoginPopup(context);
                }
              },
              child: const Icon(CupertinoIcons.person),
            ),
          ),
          SliverPadding(
            padding: .only(
              left: screenPadding.left,
              right: screenPadding.right,
              bottom: widget.bottomSafeArea + 10,
            ),
            sliver: SliverList.list(
              children: [
                Padding(
                  padding: const .only(left: 20, right: 20, top: 20),
                  child: Row(
                    children: [
                      Image.asset("assets/image1.png", width: 100, height: 100),
                      const Spacer(),
                      Obx(() {
                        if (AppState.shared.devices.isEmpty) {
                          return CupertinoButton(
                            pressedOpacity: 1,
                            color: CupertinoColors.secondarySystemFill
                                .resolveFrom(context),
                            onPressed: () {
                              AppState.shared.showBindingDevice(context);
                            },
                            borderRadius: .circular(
                              ThemeSetting.mediumCardRadius,
                            ),
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

                          // return PullDownButton(
                          //   itemBuilder: (context) {
                          //
                          //   },
                          //   buttonBuilder: (context, showMenu) => CupertinoButton(
                          //     padding: .all(15),
                          //     minimumSize: .zero,
                          //     onLongPress: () {
                          //       showUnbindingPopup();
                          //     },
                          //     pressedOpacity: 1,
                          //     borderRadius: .circular(50),
                          //     color: CupertinoColors.secondarySystemFill
                          //         .resolveFrom(context),
                          //     onPressed: showMenu,
                          //     child: Row(
                          //       mainAxisSize: .min,
                          //       spacing: 5,
                          //       children: [
                          //         if (AppState.shared.devices.length > 1)
                          //           Icon(CupertinoIcons.chevron_down, size: 25),
                          //         ConstrainedBox(
                          //           constraints: BoxConstraints(maxWidth: 150),
                          //           child: Text(
                          //             AppState.shared.deviceInfo.value
                          //                     ?.getDisplayName() ??
                          //                 "",
                          //             style: TextStyle(
                          //               fontSize: 18,
                          //               fontWeight: .bold,
                          //             ),
                          //           ),
                          //         ),
                          //       ],
                          //     ),
                          //   ),
                          // );
                        }
                      }),
                    ],
                  ),
                ),

                //====================== User info section ======================
                // CupertinoListSection.insetGrouped(
                //   children: [_buildUserProfileTile()],
                // ),
                // Obx(
                //   () => CupertinoListSection.insetGrouped(
                //     header: const Text("Devices"),
                //     children: _devices(),
                //   ),
                // ),

                //====================== General settings ======================
                CupertinoListSection.insetGrouped(children: sectionList1),

                //====================== AI agent settings ======================
                CupertinoListSection.insetGrouped(children: sectionList2),

                //====================== System settings ======================
                CupertinoListSection.insetGrouped(children: sectionList3),

                CupertinoListSection.insetGrouped(children: sectionList4),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Future<void> _launchAppStore() async {
    final storeUris = Platform.isIOS
        ? [
            Uri.parse("itms-apps://itunes.apple.com/app/id6756086326"),
            Uri.parse("https://apps.apple.com/app/id6756086326"),
          ]
        : Platform.isAndroid
        ? [
            Uri.parse("market://details?id=com.m5stack.stackchan"),
            Uri.parse(
              "https://play.google.com/store/apps/details?id=com.m5stack.stackchan",
            ),
          ]
        : [Uri.parse("https://apps.apple.com/app/id6756086326")];

    for (final uri in storeUris) {
      if (await launchUrl(uri, mode: LaunchMode.externalApplication)) {
        return;
      }
    }

    AppState.shared.showToast("Unable to open app store.");
  }

  Widget _buildChangeNameTile() {
    return CupertinoListTile(
      title: const Text("Change Device Name"),
      onTap: () => _showChangeNameDialog(),
      leading: _buildSectionIcon(
        iconPath: "assets/character.svg",
        color: CupertinoColors.activeGreen,
      ),
      trailing: _buildChevronIcon(),
    );
  }

  Widget _buildAgentConfigTile() {
    return CupertinoListTile(
      title: const Text("AI Agent Config"),
      onTap: () {
        if (_checkDeviceBinding()) {
          showCustomCupertinoSheet(
            context: context,
            useNestedNavigation: false,
            scrollableBuilder: (context, scrollController) {
              return const XiaoZhiWelcomePage(isWelCome: false);
            },
          );
        }
      },
      leading: _buildSectionIcon(
        iconPath: "assets/rectangle.badge.sparkles.fill.svg",
        color: CupertinoColors.activeGreen.resolveFrom(context),
      ),
      trailing: _buildChevronIcon(),
    );
  }

  Widget _buildMcpListTile() {
    return CupertinoListTile(
      title: const Text("MCP"),
      onTap: () {
        if (_checkDeviceBinding()) {
          showCustomCupertinoSheet(
            context: context,
            scrollableBuilder: (context, scrollController) {
              return const McpPage();
            },
          );
        }
      },
      leading: _buildSectionIcon(
        iconPath: "assets/network.badge.shield.half.filled.svg",
        color: CupertinoColors.activeBlue,
      ),
      trailing: _buildChevronIcon(),
    );
  }

  Widget _buildConversationListTile() {
    return CupertinoListTile(
      title: const Text("Chat History"),
      onTap: () {
        if (_checkDeviceBinding()) _navigateToPage(const ConversationPage());
      },
      leading: _buildSectionIcon(
        iconPath: "assets/bubble.left.and.bubble.right.fill.svg",
        color: CupertinoColors.activeOrange,
      ),
      trailing: _buildChevronIcon(),
    );
  }

  Widget _unbindResetTitle() {
    return CupertinoMenuAnchor(
      constraints: const BoxConstraints.tightFor(width: 240),
      constrainCrossAxis: true,
      consumeOutsideTaps: true,
      overlayPadding: MediaQuery.viewPaddingOf(context),
      menuChildren: AppState.shared.devices
          .map(
            (device) => CupertinoMenuItem(
              trailing: AppState.shared.deviceMac == device.mac
                  ? const Icon(CupertinoIcons.check_mark)
                  : null,
              onPressed: () {
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
                            AppState.shared.unbindDevice(device.mac);
                          },
                          child: const Text('Unbind'),
                        ),
                      ],
                    );
                  },
                );
              },
              child: Text(device.name ?? device.mac),
            ),
          )
          .toList(),
      builder: (context, controller, child) => GlassListTile(
        contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
        title: const Text("Unbind & Reset"),
        onTap: () {
          if (_checkDeviceBinding()) {
            if (controller.isOpen) {
              controller.close();
            } else {
              controller.open();
            }
          }
        },
        leading: _buildSectionIcon(
          iconPath: "assets/personalhotspot.slash.svg",
          color: CupertinoColors.destructiveRed,
        ),
        trailing: _buildChevronIcon(),
      ),
    );

    // return PullDownButton(
    //   itemBuilder: (context) => AppState.shared.devices
    //       .map(
    //         (device) => PullDownMenuItem.selectable(
    //           selected: AppState.shared.deviceMac == device.mac,
    //           iconWidget: SvgPicture.asset(
    //             "assets/personalhotspot.slash.svg",
    //             colorFilter: const ColorFilter.mode(
    //               CupertinoColors.destructiveRed,
    //               BlendMode.srcIn,
    //             ),
    //           ),
    //           onTap: () {
    //             showCupertinoDialog(
    //               context: context,
    //               builder: (context) {
    //                 return CupertinoAlertDialog(
    //                   title: const Text('Unbind Confirmation'),
    //                   content: const Text(
    //                     'Are you sure you want to unbind this device?',
    //                   ),
    //                   actions: [
    //                     CupertinoDialogAction(
    //                       child: const Text('Cancel'),
    //                       onPressed: () {
    //                         Navigator.maybePop(context);
    //                       },
    //                     ),
    //                     CupertinoDialogAction(
    //                       isDestructiveAction: true,
    //                       onPressed: () {
    //                         Navigator.maybePop(context);
    //                         AppState.shared.unbindDevice(device.mac);
    //                       },
    //                       child: const Text('Unbind'),
    //                     ),
    //                   ],
    //                 );
    //               },
    //             );
    //           },
    //           title: device.name ?? device.mac,
    //         ),
    //       )
    //       .toList(),
    //   buttonBuilder: (context, showMenu) => CupertinoListTile(
    //     title: const Text("Unbind & Reset"),
    //     onTap: () {
    //       if (_checkDeviceBinding()) showMenu();
    //     },
    //     leading: _buildSectionIcon(
    //       iconPath: "assets/personalhotspot.slash.svg",
    //       color: CupertinoColors.destructiveRed,
    //     ),
    //     trailing: _buildChevronIcon(),
    //   ),
    // );
  }

  Widget _buildBindDeviceTile() {
    return CupertinoListTile(
      title: const Text("Add a new StackChan"),
      onTap: () => AppState.shared.showBindingDevice(context),
      leading: _buildSectionIcon(
        iconPath: "assets/plus.app.svg",
        color: CupertinoColors.systemBlue,
      ),
      trailing: _buildChevronIcon(),
    );
  }

  Widget _buildSectionIcon({required String iconPath, required Color color}) {
    return Center(
      child: Container(
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: color,
          borderRadius: BorderRadius.circular(8),
        ),
        width: 28,
        height: 28,
        padding: const EdgeInsets.all(6),
        child: SvgPicture.asset(
          iconPath,
          colorFilter: const ColorFilter.mode(
            CupertinoColors.white,
            BlendMode.srcIn,
          ),
        ),
      ),
    );
  }

  Widget _buildChevronIcon() {
    return SvgPicture.asset(
      "assets/chevron.right.svg",
      width: 15,
      height: 15,
      colorFilter: ColorFilter.mode(
        CupertinoColors.secondaryLabel.resolveFrom(context),
        BlendMode.srcIn,
      ),
    );
  }

  void _showChangeNameDialog() {
    if (!_checkDeviceBinding()) return;

    String newName = AppState.shared.deviceInfo.value?.name ?? "My StackChan";
    showCupertinoDialog(
      context: context,
      builder: (context) => CupertinoAlertDialog(
        title: const Text("Please enter device name"),
        content: Padding(
          padding: const EdgeInsets.only(top: 10),
          child: CupertinoTextField(
            decoration: ThemeSetting.dialogTextFieldDecoration(context),
            controller: TextEditingController(text: newName),
            maxLength: 15,
            maxLines: 1,
            autofocus: true,
            inputFormatters: [
              // Enforce the input length limit for a smoother editing experience.
              LengthLimitingTextInputFormatter(15),
            ],
            onChanged: (value) => newName = value,
          ),
        ),
        actions: [
          CupertinoDialogAction(
            onPressed: () => Navigator.maybePop(context),
            child: const Text("Cancel"),
          ),
          CupertinoDialogAction(
            isDefaultAction: true,
            onPressed: () {
              if (newName.trim().isNotEmpty) {
                AppState.shared.deviceInfo.value?.name = newName.trim();
                AppState.shared.updateDeviceInfo();
              }
              Navigator.maybePop(context);
            },
            child: const Text("Confirm"),
          ),
        ],
      ),
    );
  }
}

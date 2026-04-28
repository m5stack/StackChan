/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';
import 'package:flutter_slidable/flutter_slidable.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:get/get.dart';
import 'package:pull_down_button/pull_down_button.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/view/home/conversation_page.dart';
import 'package:stack_chan/view/home/mcp_page.dart';
import 'package:stack_chan/view/popup/user_info_page.dart';

import '../popup/xiaozhi_welcome_page.dart';

class Settings extends StatefulWidget {
  const Settings({super.key});

  @override
  State<StatefulWidget> createState() => _SettingsState();
}

class _SettingsState extends State<Settings> {
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (AppState.shared.isLogin.value) {
        AppState.shared.getUserInfo();
        AppState.shared.getDeviceInfo();
      }
    });
  }

  @override
  void dispose() {
    if (mounted) {
      FocusScope.of(context).unfocus();
    }
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
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      child: CustomScrollView(
        slivers: [
          CupertinoSliverNavigationBar(
            largeTitle: Text("Settings"),
            trailing: CupertinoButton(
              padding: .zero,
              child: Icon(
                CupertinoIcons.person_alt_circle,
                color: CupertinoTheme.of(context).primaryColor,
                size: 44,
              ),
              onPressed: () {
                if (AppState.shared.isLogin.value) {
                  showCupertinoSheet(
                    context: context,
                    showDragHandle: true,
                    builder: (context) {
                      return UserInfoPage();
                    },
                  );
                } else {
                  AppState.shared.showLoginPopup(context);
                }
              },
            ),
          ),
          SliverList.list(
            children: [
              Padding(
                padding: .all(20),
                child: Row(
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
                                        value.mac == AppState.shared.deviceMac,
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
                                onTap: () =>
                                    AppState.shared.showBindingDevice(context),
                                title: "Add a new StackChan",
                              ),
                            );
                            return list;
                          },
                          buttonBuilder: (context, showMenu) => CupertinoButton(
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
                                  Icon(CupertinoIcons.chevron_down, size: 25),
                                ConstrainedBox(
                                  constraints: BoxConstraints(maxWidth: 150),
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
              ),

              //====================== userinfo ======================
              // CupertinoListSection.insetGrouped(
              //   children: [_buildUserProfileTile()],
              // ),
              // Obx(
              //   () => CupertinoListSection.insetGrouped(
              //     header: const Text("Devices"),
              //     children: _devices(),
              //   ),
              // ),

              //====================== set ======================
              CupertinoListSection.insetGrouped(
                children: [_buildChangeNameTile()],
              ),

              //====================== AIset ======================
              CupertinoListSection.insetGrouped(
                children: [
                  _buildAgentConfigTile(),
                  _buildMcpListTile(),
                  _buildConversationListTile(),
                ],
              ),

              //====================== Systemset ======================
              CupertinoListSection.insetGrouped(
                children: [_unbindResetTitle(), _buildBindDeviceTile()],
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _buildUserProfileTile() {
    return CupertinoListTile(
      onTap: () {
        if (AppState.shared.isLogin.value) {
          showCupertinoSheet(
            context: context,
            showDragHandle: true,
            builder: (context) {
              return UserInfoPage();
            },
          );
        } else {
          AppState.shared.showLoginPopup(context);
        }
      },
      padding: .symmetric(vertical: 10, horizontal: 20),
      title: Obx(() {
        final user = AppState.shared.userInfo.value;
        final title = AppState.shared.isLogin.value
            ? (user?.displayName?.isNotEmpty == true
                  ? user!.displayName!
                  : "Name")
            : "Please Login";
        return Text(title, style: TextStyle(fontSize: 18, fontWeight: .bold));
      }),
      leading: ClipOval(
        child: Container(
          color: CupertinoTheme.of(context).primaryColor,
          alignment: Alignment.center,
          child: const Icon(
            CupertinoIcons.person_fill,
            color: CupertinoColors.white,
            size: 30,
          ),
        ),
      ),
      leadingSize: 50,
      trailing: SvgPicture.asset(
        "assets/chevron.right.svg",
        width: 15,
        height: 15,
        colorFilter: ColorFilter.mode(
          CupertinoColors.secondaryLabel.resolveFrom(context),
          BlendMode.srcIn,
        ),
      ),
    );
  }

  List<Widget> _devices() {
    if (AppState.shared.devices.isNotEmpty) {
      return AppState.shared.devices
          .map(
            (value) => Slidable(
              key: Key(value.mac),
              endActionPane: ActionPane(
                extentRatio: 0.25,
                motion: const ScrollMotion(),
                children: [
                  SlidableAction(
                    onPressed: (_) {
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
                                  AppState.shared.unbindDevice(value.mac);
                                },
                                child: const Text('Unbind'),
                              ),
                            ],
                          );
                        },
                      );
                    },
                    backgroundColor: CupertinoColors.systemOrange.resolveFrom(
                      context,
                    ),
                    foregroundColor: CupertinoColors.white,
                    icon: CupertinoIcons.link,
                    label: 'Unbind',
                  ),
                ],
              ),
              child: CupertinoListTile(
                leading: Image.asset(
                  "assets/image1.png",
                  width: 28,
                  height: 28,
                ),
                title: Text(value.getDisplayName()),
                trailing: value.mac == AppState.shared.deviceMac
                    ? SvgPicture.asset(
                        "assets/checkmark.svg",
                        width: 15,
                        height: 15,
                        colorFilter: ColorFilter.mode(
                          CupertinoColors.secondaryLabel.resolveFrom(context),
                          BlendMode.srcIn,
                        ),
                      )
                    : SizedBox.shrink(),
                onTap: () {
                  if (AppState.shared.deviceMac != value.mac) {
                    AppState.shared.switchDevice(value);
                  }
                },
              ),
            ),
          )
          .toList();
    } else {
      return [
        CupertinoListTile(
          title: Center(child: Text("No device bound")),
          onTap: () {
            AppState.shared.showBindingDevice(context);
          },
        ),
      ];
    }
  }

  Widget _buildChangeNameTile() {
    return CupertinoListTile(
      title: const Text("Change Device Name"),
      onTap: () => _showChangeNameDialog(),
      leading: _buildSectionIcon(
        iconPath: "assets/character.svg",
        color: CupertinoColors.activeGreen,
      ),
      trailing: Row(
        mainAxisSize: MainAxisSize.min,
        spacing: 15,
        children: [
          // Obx(
          //   () => Text(
          //     AppState.shared.deviceInfo.value?.name ?? "",
          //     style: TextStyle(
          //       color: CupertinoColors.secondaryLabel.resolveFrom(context),
          //     ),
          //   ),
          // ),
          _buildChevronIcon(),
        ],
      ),
    );
  }

  Widget _configureWiFi() {
    return CupertinoListTile(
      title: const Text("Configure Wi-Fi"),
      onTap: () {},
      leading: _buildSectionIcon(
        iconPath: "assets/wifi.svg",
        color: CupertinoColors.activeBlue,
      ),
      trailing: _buildChevronIcon(),
    );
  }

  Widget _buildAgentConfigTile() {
    return CupertinoListTile(
      title: const Text("AI Agent Config"),
      onTap: () {
        if (_checkDeviceBinding()) {
          showCupertinoSheet(
            context: context,
            builder: (context) {
              return XiaoZhiWelcomePage(isWelCome: false);
            },
          );
          // showCupertinoSheet(
          //   useNestedNavigation: true,
          //   context: context,
          //   builder: (context) => const AgentConfiguration(),
          // );
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
          showCupertinoSheet(
            context: context,
            builder: (context) {
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
    return PullDownButton(
      itemBuilder: (context) => AppState.shared.devices
          .map(
            (device) => PullDownMenuItem.selectable(
              selected: AppState.shared.deviceMac == device.mac,
              iconWidget: SvgPicture.asset(
                "assets/personalhotspot.slash.svg",
                colorFilter: const ColorFilter.mode(
                  CupertinoColors.destructiveRed,
                  BlendMode.srcIn,
                ),
              ),
              onTap: () {
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
                            AppState.shared.unbindDevice(device.mac);
                          },
                          child: const Text('Unbind'),
                        ),
                      ],
                    );
                  },
                );
              },
              title: device.name ?? device.mac,
            ),
          )
          .toList(),
      buttonBuilder: (context, showMenu) => CupertinoListTile(
        title: const Text("Unbind & Reset"),
        onTap: () {
          if (_checkDeviceBinding()) showMenu();
        },
        leading: _buildSectionIcon(
          iconPath: "assets/personalhotspot.slash.svg",
          color: CupertinoColors.destructiveRed,
        ),
        trailing: _buildChevronIcon(),
      ),
    );
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
    return Container(
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
            controller: TextEditingController(text: newName),
            maxLength: 15,
            maxLines: 1,
            autofocus: true,
            inputFormatters: [
              //addinputlimit,
              LengthLimitingTextInputFormatter(15),
            ],
            onChanged: (value) => newName = value,
          ),
        ),
        actions: [
          CupertinoDialogAction(
            onPressed: () => Navigator.pop(context),
            child: const Text("Cancel"),
          ),
          CupertinoDialogAction(
            isDefaultAction: true,
            onPressed: () {
              if (newName.trim().isNotEmpty) {
                AppState.shared.deviceInfo.value?.name = newName.trim();
                AppState.shared.updateDeviceInfo();
              }
              Navigator.pop(context);
            },
            child: const Text("Confirm"),
          ),
        ],
      ),
    );
  }
}

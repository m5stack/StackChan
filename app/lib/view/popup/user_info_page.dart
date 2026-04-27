/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';
import 'package:get/get.dart';
import 'package:stack_chan/app_state.dart';

class UserInfoPage extends StatefulWidget {
  const UserInfoPage({super.key});

  @override
  State<StatefulWidget> createState() => _UserInfoPageState();
}

class _UserInfoPageState extends State<UserInfoPage> {
  @override
  void initState() {
    super.initState();
    if (AppState.shared.userInfo.value == null) {
      AppState.shared.getUserInfo();
    }
  }

  //time戳to日期
  String _formatTimestamp(int? timestamp) {
    if (timestamp == null) return "Unknown";
    final date = DateTime.fromMillisecondsSinceEpoch(timestamp);
    return "${date.year}-${date.month.toString().padLeft(2, '0')}-${date.day.toString().padLeft(2, '0')}";
  }

  //emailvalidatestate
  String _getEmailStatus(int? status) {
    if (status == 1) return "Verified";
    return "Unverified";
  }

  @override
  Widget build(BuildContext context) {
    return ClipRSuperellipse(
      borderRadius: .circular(12),
      child: CupertinoPageScaffold(
        backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
          context,
        ),
        navigationBar: CupertinoNavigationBar(
          middle: Obx(
            () =>
                Text(AppState.shared.userInfo.value?.displayName ?? "Profile"),
          ),
          trailing: CupertinoButton(
            padding: .zero,
            child: Icon(
              CupertinoIcons.xmark_circle_fill,
              size: 25,
              color: CupertinoColors.separator.resolveFrom(context),
            ),
            onPressed: () {
              Navigator.pop(context);
            },
          ),
        ),
        child: Obx(() {
          if (AppState.shared.userInfo.value == null) {
            return const Center(child: CupertinoActivityIndicator(radius: 14));
          }

          final userInfo = AppState.shared.userInfo.value!;
          return ListView(
            children: [
              //====================== center文字圆Avatar ======================
              Center(
                child: Container(
                  width: 100,
                  height: 100,
                  decoration: BoxDecoration(
                    color:
                        _hexToColor(userInfo.iconBgColor) ??
                        CupertinoColors.systemBlue,
                    shape: BoxShape.circle,
                  ),
                  child: Center(
                    child: Text(
                      userInfo.iconText ?? "?",
                      style: const TextStyle(
                        color: CupertinoColors.white,
                        fontSize: 36,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                  ),
                ),
              ),
              const SizedBox(height: 30),

              CupertinoListSection.insetGrouped(
                children: [
                  _buildInfoItem(
                    "User ID",
                    userInfo.uid?.toString() ?? "Unknown",
                  ),
                  _buildInfoItem("Username", userInfo.username ?? "Unknown"),
                  _buildInfoItem(
                    "Display Name",
                    userInfo.displayName ?? "Unknown",
                  ),
                  _buildInfoItem("User Slug", userInfo.userslug ?? "Unknown"),
                  _buildInfoItem(
                    "Account Status",
                    userInfo.userStatus ?? "Normal",
                  ),
                  _buildInfoItem(
                    "Email Verification",
                    _getEmailStatus(userInfo.emailConfirmed),
                  ),
                  _buildInfoItem(
                    "Registered",
                    _formatTimestamp(userInfo.joinDate),
                  ),
                  _buildInfoItem(
                    "Last Online",
                    _formatTimestamp(userInfo.lastOnline),
                  ),
                ],
              ),

              //====================== exitloginbutton ======================
              CupertinoListSection.insetGrouped(
                children: [
                  CupertinoListTile(
                    title: Center(
                      child: Text(
                        "Log Out",
                        style: TextStyle(
                          color: CupertinoColors.destructiveRed.resolveFrom(
                            context,
                          ),
                          fontWeight: FontWeight.w500,
                        ),
                      ),
                    ),
                    onTap: () {
                      _showLogoutDialog();
                    },
                  ),
                ],
              ),
            ],
          );
        }),
      ),
    );
  }

  //info行component
  Widget _buildInfoItem(String title, String value) {
    return CupertinoListTile(
      title: Text(title, style: TextStyle(fontWeight: FontWeight.w500)),
      trailing: Text(value),
    );
  }

  //exitconfirmpopup
  void _showLogoutDialog() {
    showCupertinoDialog(
      context: context,
      builder: (context) => CupertinoAlertDialog(
        title: const Text("Confirm Log Out"),
        content: const Text("You will need to log in again after logging out."),
        actions: [
          CupertinoDialogAction(
            child: Text("Cancel"),
            onPressed: () => Navigator.of(context).pop(),
          ),
          CupertinoDialogAction(
            isDestructiveAction: true,
            onPressed: () async {
              Navigator.of(context).pop();
              await AppState.shared.logout();
              if (mounted) {
                Navigator.of(this.context).pop();
              }
            },
            child: const Text("Log Out"),
          ),
        ],
      ),
    );
  }

  //HexcolortoFluttercolor
  Color? _hexToColor(String? hexString) {
    if (hexString == null || hexString.isEmpty) return null;
    hexString = hexString.replaceAll("#", "");
    if (hexString.length == 6) hexString = "FF$hexString";
    try {
      return Color(int.parse("0x$hexString"));
    } catch (e) {
      return null;
    }
  }
}

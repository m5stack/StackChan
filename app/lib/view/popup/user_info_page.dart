/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/theme_setting.dart';
import 'package:get/get.dart';
import 'package:stack_chan/app_state.dart';

class UserInfoPage extends StatefulWidget {
  const UserInfoPage({super.key, this.scrollController});

  final ScrollController? scrollController;

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

  // Format a millisecond timestamp as a yyyy-MM-dd date.
  String _formatTimestamp(int? timestamp) {
    if (timestamp == null) return "Unknown";
    final date = DateTime.fromMillisecondsSinceEpoch(timestamp);
    return "${date.year}-${date.month.toString().padLeft(2, '0')}-${date.day.toString().padLeft(2, '0')}";
  }

  // Email verification status label.
  String _getEmailStatus(int? status) {
    if (status == 1) return "Verified";
    return "Unverified";
  }

  @override
  Widget build(BuildContext context) {
    final padding = MediaQuery.paddingOf(context);
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      navigationBar: CupertinoNavigationBar(
        backgroundColor: ThemeSetting.cupertinoNavigationBarBackgroundColor(
          context,
        ),
        border: ThemeSetting.cupertinoNavigationBarBorder(context),
        padding: ThemeSetting.navigationBarPadding,
        middle: Obx(
          () => Text(AppState.shared.userInfo.value?.displayName ?? "Profile"),
        ),
        leading: const SizedBox.shrink(),
        trailing: CupertinoButton(
          sizeStyle: .medium,
          onPressed: () {
            Navigator.maybePop(context);
          },
          child: const Icon(CupertinoIcons.xmark),
        ),
      ),
      child: Obx(() {
        if (AppState.shared.userInfo.value == null) {
          return const Center(child: CupertinoActivityIndicator(radius: 14));
        }

        final userInfo = AppState.shared.userInfo.value!;
        return ListView(
          padding: .only(
            top: padding.top + kMinInteractiveDimensionCupertino,
            bottom: padding.bottom + 10,
            right: padding.right,
            left: padding.left,
          ),
          controller: widget.scrollController,
          children: [
            const SizedBox(height: 30),
            //====================== Centered avatar ======================
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

            //====================== Log out button ======================
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
    );
  }

  // Single label/value info row.
  Widget _buildInfoItem(String title, String value) {
    return CupertinoListTile(
      title: Text(title, style: const TextStyle(fontWeight: FontWeight.w500)),
      trailing: Text(value),
    );
  }

  // Log-out confirmation dialog.
  void _showLogoutDialog() {
    showCupertinoDialog(
      context: context,
      builder: (context) => CupertinoAlertDialog(
        title: const Text("Confirm Log Out"),
        content: const Text("You will need to log in again after logging out."),
        actions: [
          CupertinoDialogAction(
            child: const Text("Cancel"),
            onPressed: () => Navigator.maybePop(context),
          ),
          CupertinoDialogAction(
            isDestructiveAction: true,
            onPressed: () async {
              Navigator.maybePop(context);
              await AppState.shared.logout();
              if (mounted) {
                Navigator.maybePop(this.context);
              }
            },
            child: const Text("Log Out"),
          ),
        ],
      ),
    );
  }

  // Convert a hex string such as "#RRGGBB" to a Flutter color.
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

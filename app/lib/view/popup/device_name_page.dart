/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

//====================== Device name page ======================
import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/theme_setting.dart';
import 'package:stack_chan/view/popup/XiaoZhi_welcome_page.dart';

import '../../app_state.dart';
import 'device_wifi_config.dart';

class DeviceNamePage extends StatefulWidget {
  const DeviceNamePage({super.key});

  @override
  State<StatefulWidget> createState() => _DeviceNamePageState();
}

class _DeviceNamePageState extends State<DeviceNamePage> {
  late TextEditingController _nameController;

  @override
  void initState() {
    super.initState();
    _nameController = TextEditingController(text: "My StackChan");
  }

  @override
  void dispose() {
    FocusManager.instance.primaryFocus?.unfocus();
    _nameController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final padding = MediaQuery.paddingOf(context);

    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      child: Column(
        children: [
          Expanded(
            child: CustomScrollView(
              slivers: [
                CupertinoSliverNavigationBar(
                  backgroundColor:
                      ThemeSetting.cupertinoNavigationBarBackgroundColor(
                        context,
                      ),
                  border: ThemeSetting.cupertinoNavigationBarBorder(context),
                  padding: ThemeSetting.navigationBarPadding,
                  stretch: true,
                  largeTitle: const Text("Device Name"),
                ),
                SliverPadding(
                  padding: .only(left: padding.left, right: padding.right),
                  sliver: SliverList.list(
                    children: [
                      const SizedBox(height: 15),
                      Image.asset(
                        "assets/lateral_image.png",
                        width: double.infinity,
                        height: 120,
                        fit: BoxFit.contain,
                      ),
                      const SizedBox(height: 15),
                      CupertinoListSection.insetGrouped(
                        header: const Text("Give this StackChan a name"),
                        children: [
                          CupertinoTextField(
                            maxLines: 1,
                            padding: const .all(12),
                            textAlign: .center,
                            decoration: const BoxDecoration(),
                            controller: _nameController,
                            placeholder: "Enter device name",
                            clearButtonMode: OverlayVisibilityMode.editing,
                            autofocus: true,
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
          Padding(
            padding: .only(
              left: 20 + padding.left,
              right: 20 + padding.right,
              top: 20,
              bottom: padding.bottom + 20,
            ),
            child: Row(
              spacing: 15,
              children: [
                Expanded(
                  child: CupertinoButton.tinted(
                    child: Center(
                      child: Text(
                        "Skip",
                        style: TextStyle(
                          color: CupertinoTheme.of(context).primaryColor,
                        ),
                      ),
                    ),
                    onPressed: () {
                      if (mounted) {
                        FocusScope.of(context).unfocus();
                      }
                      Navigator.of(context).push(
                        CupertinoPageRoute(
                          builder: (context) {
                            return const DeviceWifiConfig();
                          },
                        ),
                      );
                    },
                  ),
                ),
                Expanded(
                  child: CupertinoButton.filled(
                    child: const Center(
                      child: Text(
                        "Continue",
                        style: TextStyle(color: CupertinoColors.white),
                      ),
                    ),
                    onPressed: () async {
                      if (mounted) {
                        FocusScope.of(context).unfocus();
                      }
                      if (_nameController.text.trim().isEmpty) {
                        AppState.shared.showToast("Please enter device name");
                        return;
                      }
                      AppState.shared.deviceInfo.value?.name = _nameController
                          .text
                          .trim();
                      AppState.shared.updateDeviceInfo();
                      Navigator.of(context).push(
                        CupertinoPageRoute(
                          builder: (context) {
                            return const XiaoZhiWelcomePage(isWelCome: true);
                          },
                        ),
                      );
                    },
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

//====================== devicename(completed) ======================
import 'package:flutter/cupertino.dart';
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
    _nameController.dispose();
    if (mounted) {
      FocusScope.of(context).unfocus();
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      child: Column(
        children: [
          Expanded(
            child: CustomScrollView(
              slivers: [
                CupertinoSliverNavigationBar(largeTitle: Text("Device Name")),
                SliverList.list(
                  children: [
                    Image.asset(
                      "assets/lateral_image.png",
                      width: double.infinity,
                      height: 120,
                      fit: BoxFit.contain,
                    ),
                    SizedBox(height: 15),
                    CupertinoListSection.insetGrouped(
                      header: Text("Give this StackChan a name"),
                      children: [
                        CupertinoListTile(
                          padding: .all(15),
                          title: CupertinoTextField(
                            decoration: BoxDecoration(),
                            controller: _nameController,
                            placeholder: "Enter device name",
                            clearButtonMode: OverlayVisibilityMode.editing,
                            autofocus: true,
                          ),
                        ),
                      ],
                    ),
                  ],
                ),
              ],
            ),
          ),
          Padding(
            padding: .only(
              left: 15,
              right: 15,
              top: 15,
              bottom: MediaQuery.paddingOf(context).bottom + 15,
            ),
            child: Row(
              spacing: 15,
              children: [
                Expanded(
                  child: CupertinoButton.tinted(
                    child: Text("Skip"),
                    onPressed: () {
                      if (mounted) {
                        FocusScope.of(context).unfocus();
                      }
                      Navigator.of(context).push(
                        CupertinoPageRoute(
                          builder: (context) {
                            return DeviceWifiConfig();
                          },
                        ),
                      );
                    },
                  ),
                ),
                Expanded(
                  child: CupertinoButton.filled(
                    child: Text("Continue"),
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
                            return XiaoZhiWelcomePage(isWelCome: true);
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

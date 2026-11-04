/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/theme_setting.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/model/XiaoZhi/device.dart';
import 'package:stack_chan/util/xiao_zhi_util.dart';

import '../../model/XiaoZhi/endpoints_response.dart';
import '../../model/XiaoZhi/mcp_endpoints.dart';

class McpPage extends StatefulWidget {
  const McpPage({super.key});

  @override
  State<StatefulWidget> createState() => _McpPageState();
}

class _McpPageState extends State<McpPage> {
  final RxList<McpEndpoints> mcpEndpoints = RxList([]);
  final RxBool isLoading = false.obs;
  late final TextEditingController _nameController;
  late final TextEditingController _descController;

  final RxnString endpointToken = RxnString();

  final RxList<Tool> toolList = RxList([]);

  @override
  void initState() {
    super.initState();
    _nameController = TextEditingController();
    _descController = TextEditingController();
    // fetchMcpList();
    getAgent();
  }

  final Rxn<Device> device = Rxn();

  Future<void> getAgent() async {
    if (AppState.shared.deviceMac.isNotEmpty) {
      final devices = await XiaoZhiUtil.shared.getDevice(
        AppState.shared.deviceMac,
      );
      if (devices.isNotEmpty) {
        device.value = devices.first;
        if (device.value!.agentId != null) {
          getToken(device.value!.agentId!);
          getEndpointsList(device.value!.agentId!);
        }
      }
    }
  }

  Future<void> getEndpointsList(int agentId) async {
    final data = await XiaoZhiUtil.shared.endpointsList(agentId);
    if (data != null) {
      if (data.endpoints.isNotEmpty) {
        if (data.endpoints.first.tools.isNotEmpty) {
          toolList.value = data.endpoints.first.tools;
        }
      }
    }
  }

  Future<void> getToken(int id) async {
    final token = await XiaoZhiUtil.shared.generateMcpEndpointToken(id);
    if (token != null) {
      endpointToken.value = token;
    }
  }

  @override
  void dispose() {
    FocusManager.instance.primaryFocus?.unfocus();
    _nameController.dispose();
    _descController.dispose();
    super.dispose();
  }

  Future<void> fetchMcpList() async {
    isLoading.value = true;
    mcpEndpoints.value = await XiaoZhiUtil.shared.mcpEndpoints();
    isLoading.value = false;
  }

  final RxBool enabled = RxBool(false);

  void showMcpDialog({McpEndpoints? editItem}) {
    if (editItem != null) {
      _nameController.text = editItem.name ?? "";
      _descController.text = editItem.description ?? "";
    } else {
      _nameController.clear();
      _descController.clear();
    }
    enabled.value = editItem?.enabled == 1;
    showCupertinoDialog(
      context: context,
      builder: (context) {
        return CupertinoAlertDialog(
          title: Text(
            editItem == null
                ? "New MCP access point added"
                : "Editorial access point",
          ),
          content: Column(
            spacing: 10,
            children: [
              CupertinoTextField(
                decoration: ThemeSetting.dialogTextFieldDecoration(context),
                controller: _nameController,
                placeholder: "Please enter the name.",
              ),
              CupertinoTextField(
                decoration: ThemeSetting.dialogTextFieldDecoration(context),
                controller: _descController,
                placeholder: "Please provide the description.",
              ),
              Row(
                mainAxisAlignment: .spaceBetween,
                children: [
                  const Text("Enabled status"),
                  Obx(
                    () => CupertinoSwitch(
                      value: enabled.value,
                      onChanged: (value) => enabled.value = value,
                    ),
                  ),
                ],
              ),
            ],
          ),
          actions: [
            CupertinoDialogAction(
              child: const Text('Cancel'),
              onPressed: () => Navigator.maybePop(context),
            ),
            CupertinoDialogAction(
              isDestructiveAction: false,
              isDefaultAction: true,
              child: const Text("Confirm"),
              onPressed: () async {
                final name = _nameController.text.trim();
                final desc = _descController.text.trim();
                if (name.isEmpty) {
                  AppState.shared.showToast("The name cannot be left blank.");
                  return;
                }
                Navigator.maybePop(context);
                if (editItem == null) {
                  await XiaoZhiUtil.shared.createMcpEndpoints(
                    name,
                    desc,
                    enabled.value,
                  );
                } else {
                  await XiaoZhiUtil.shared.editEndpoints(
                    editItem.id!,
                    name: name,
                    description: desc,
                    enabled: enabled.value,
                  );
                }
                fetchMcpList();
              },
            ),
          ],
        );
      },
    );
  }

  void showDeleteDialog(int id) {
    showCupertinoDialog(
      context: context,
      builder: (context) {
        return CupertinoAlertDialog(
          title: const Text("Are you sure you want to delete?"),
          content: const Text(
            "Are you sure you want to delete this access point? This operation is irreversible.",
          ),
          actions: [
            CupertinoDialogAction(
              child: const Text('Cancel'),
              onPressed: () => Get.back(),
            ),
            CupertinoDialogAction(
              isDestructiveAction: true,
              child: const Text('Delete'),
              onPressed: () async {
                Navigator.maybePop(context);
                final success = await XiaoZhiUtil.shared.deleteEndpoints(id);
                if (success) {
                  fetchMcpList();
                  AppState.shared.showToast("successfully delete");
                }
              },
            ),
          ],
        );
      },
    );
  }

  Future<void> generateAndCopyToken(int id) async {
    isLoading.value = true;
    final token = await XiaoZhiUtil.shared.getEndpointToken(id);
    isLoading.value = false;

    if (token != null && token.isNotEmpty) {
      await Clipboard.setData(ClipboardData(text: token));
      AppState.shared.showToast("Token has been copied");
    } else {
      AppState.shared.showToast("Failed to generate token");
    }
  }

  @override
  Widget build(BuildContext context) {
    final padding = MediaQuery.paddingOf(context);

    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      navigationBar: CupertinoNavigationBar.large(
        backgroundColor: ThemeSetting.cupertinoNavigationBarBackgroundColor(
          context,
        ),
        border: ThemeSetting.cupertinoNavigationBarBorder(context),
        padding: ThemeSetting.navigationBarPadding,
        largeTitle: const Text("MCP"),
        leading: const SizedBox.shrink(),
        trailing: CupertinoButton(
          sizeStyle: .medium,
          child: const Icon(CupertinoIcons.xmark),
          onPressed: () {
            Navigator.maybePop(context);
          },
        ),
      ),
      child: Padding(
        padding: EdgeInsets.only(
          bottom: MediaQuery.paddingOf(context).bottom,
          top:
              MediaQuery.paddingOf(context).top +
              kMinInteractiveDimensionCupertino +
              52,
          left: padding.left,
          right: padding.right,
        ),
        child: Column(
          children: [
            Expanded(
              child: Obx(
                () => ListView(
                  padding: .zero,
                  children: [
                    CupertinoListSection.insetGrouped(
                      header: const Text("Access Point Status"),
                      children: toolList.isEmpty
                          ? [
                              const CupertinoListTile(
                                title: Center(child: Text("Offline")),
                              ),
                            ]
                          : toolList
                                .map(
                                  (tool) =>
                                      CupertinoListTile(title: Text(tool.name)),
                                )
                                .toList(),
                    ),
                    CupertinoListSection.insetGrouped(
                      header: const Text("Access point address"),
                      children: [
                        if (endpointToken.value != null)
                          CupertinoListTile(
                            title: Padding(
                              padding: const .all(5),
                              child: Text(
                                softWrap: true,
                                maxLines: 100,
                                "wss://api.xiaozhi.me/mcp/?token=${endpointToken.value}",
                              ),
                            ),
                            trailing: CupertinoButton(
                              child: const Icon(CupertinoIcons.doc_on_doc),
                              onPressed: () async {
                                await Clipboard.setData(
                                  ClipboardData(
                                    text:
                                        "wss://api.xiaozhi.me/mcp/?token=${endpointToken.value}",
                                  ),
                                );
                                AppState.shared.showToast("Already copied");
                              },
                            ),
                            onTap: () async {
                              await Clipboard.setData(
                                ClipboardData(
                                  text:
                                      "wss://api.xiaozhi.me/mcp/?token=${endpointToken.value}",
                                ),
                              );
                              AppState.shared.showToast("Already copied");
                            },
                          )
                        else
                          const CupertinoListTile(
                            title: Center(child: Text("Loading...")),
                          ),
                      ],
                    ),
                  ],
                ),
              ),
            ),
            Padding(
              padding: .only(
                left: 20,
                right: 20,
                top: 20,
                bottom: MediaQuery.paddingOf(context).bottom + 20,
              ),
              child: SizedBox(
                width: .infinity,
                child: CupertinoButton.filled(
                  child: const Text(
                    "OK",
                    style: TextStyle(color: CupertinoColors.white),
                  ),
                  onPressed: () {
                    Navigator.maybePop(context);
                  },
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

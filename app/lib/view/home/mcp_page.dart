/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/model/XiaoZhi/device.dart';
import 'package:stack_chan/util/XiaoZhi_util.dart';

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
        if (device.value!.agent_id != null) {
          getToken(device.value!.agent_id!);
          getEndpointsList(device.value!.agent_id!);
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
    _nameController.dispose();
    _descController.dispose();
    if (mounted) {
      FocusScope.of(context).unfocus();
    }
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
                controller: _nameController,
                placeholder: "Please enter the name.",
              ),
              CupertinoTextField(
                controller: _descController,
                placeholder: "Please provide the description.",
              ),
              Row(
                mainAxisAlignment: .spaceBetween,
                children: [
                  Text("Enabled status"),
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
              onPressed: () => Navigator.pop(context),
            ),
            CupertinoDialogAction(
              isDestructiveAction: false,
              isDefaultAction: true,
              child: Text("Confirm"),
              onPressed: () async {
                final name = _nameController.text.trim();
                final desc = _descController.text.trim();
                if (name.isEmpty) {
                  AppState.shared.showToast("The name cannot be left blank.");
                  return;
                }
                Navigator.pop(context);
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
          title: Text("Are you sure you want to delete?"),
          content: Text(
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
                Navigator.pop(context);
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

  Widget buildItem(McpEndpoints item) {
    return CupertinoListTile(
      title: Text(item.name ?? "Unnamed"),
      subtitle: Column(
        crossAxisAlignment: .start,
        children: [
          Text(
            item.description ?? "No description",
            style: TextStyle(fontSize: 12),
          ),
          const SizedBox(height: 4),
          Row(
            children: [
              Text(
                item.enabled == 1 ? "Enabled" : "Disabled",
                style: TextStyle(
                  fontSize: 11,
                  color: item.enabled == 1
                      ? CupertinoColors.activeGreen
                      : CupertinoColors.systemRed,
                ),
              ),
              const SizedBox(width: 10),
              Text(
                item.createdAt ?? "",
                style: TextStyle(
                  fontSize: 11,
                  color: CupertinoColors.systemGrey,
                ),
              ),
            ],
          ),
        ],
      ),
      trailing: Row(
        mainAxisSize: .min,
        children: [
          CupertinoButton(
            padding: .zero,
            child: const Icon(CupertinoIcons.doc_on_clipboard, size: 18),
            onPressed: () => generateAndCopyToken(item.id!),
          ),
          CupertinoButton(
            padding: .zero,
            child: const Icon(CupertinoIcons.pencil, size: 18),
            onPressed: () => showMcpDialog(editItem: item),
          ),
          CupertinoButton(
            padding: .zero,
            child: Icon(
              CupertinoIcons.trash,
              size: 18,
              color: CupertinoColors.destructiveRed,
            ),
            onPressed: () => showDeleteDialog(item.id!),
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      navigationBar: CupertinoNavigationBar.large(
        backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
          context,
        ),
        largeTitle: Text("MCP"),
      ),
      child: Padding(
        padding: EdgeInsets.only(
          bottom: 15 + MediaQuery.of(context).padding.bottom,
          left: 15,
          right: 15,
        ),
        child: Column(
          children: [
            Expanded(
              child: Obx(
                () => ListView(
                  padding: .zero,
                  children: [
                    CupertinoListSection.insetGrouped(
                      header: Text("Access Point Status"),
                      children: toolList.isEmpty
                          ? [
                              CupertinoListTile(
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
                      header: Text("Access point address"),
                      children: [
                        if (endpointToken.value != null)
                          CupertinoListTile(
                            title: Padding(
                              padding: .all(5),
                              child: Text(
                                softWrap: true,
                                maxLines: 100,
                                "wss://api.XiaoZhi.me/mcp/?token=${endpointToken.value}",
                              ),
                            ),
                            trailing: CupertinoButton(
                              child: Icon(CupertinoIcons.doc_on_doc),
                              onPressed: () async {
                                await Clipboard.setData(
                                  ClipboardData(
                                    text:
                                        "wss://api.XiaoZhi.me/mcp/?token=${endpointToken.value}",
                                  ),
                                );
                                AppState.shared.showToast("Already copied");
                              },
                            ),
                            onTap: () async {
                              await Clipboard.setData(
                                ClipboardData(
                                  text:
                                      "wss://api.XiaoZhi.me/mcp/?token=${endpointToken.value}",
                                ),
                              );
                              AppState.shared.showToast("Already copied");
                            },
                          )
                        else
                          CupertinoListTile(
                            title: Center(child: Text("Loading...")),
                          ),
                      ],
                    ),
                  ],
                ),
              ),
            ),
            CupertinoButton.filled(
              child: SizedBox(
                width: .infinity,
                child: Center(child: Text("OK")),
              ),
              onPressed: () {
                Navigator.pop(context);
              },
            ),
          ],
        ),
      ),
    );
  }
}

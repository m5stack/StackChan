/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';
import 'package:get/get.dart';
import 'package:pull_down_button/pull_down_button.dart';
import 'package:stack_chan/model/XiaoZhi/agent.dart';
import 'package:stack_chan/util/extension.dart';
import 'package:stack_chan/util/music_util.dart';
import 'package:stack_chan/view/popup/device_wifi_config.dart';

import '../../app_state.dart';
import '../../model/XiaoZhi/XiaoZhi_model.dart';
import '../../model/XiaoZhi/agent_create.dart';
import '../../model/XiaoZhi/common_mcp_tool.dart';
import '../../model/XiaoZhi/tts_list.dart';
import '../../util/XiaoZhi_util.dart';
import '../../util/mac_address_validator.dart';
import '../../util/value_constant.dart';

class XiaoZhiWelcomePage extends StatefulWidget {
  const XiaoZhiWelcomePage({super.key, this.isWelCome});

  final bool? isWelCome;

  @override
  State<StatefulWidget> createState() => _XiaoZhiWelcomePageState();
}

class XiaoZhiEditAgentModel extends GetxController {
  Agent? agent;

  final RxBool isLoading = false.obs;

  late TextEditingController agentNameController;
  late TextEditingController assistantNameController;
  late TextEditingController characterController;
  late TextEditingController memoryController;

  final Rxn<ModelData> selectedModel = Rxn();
  final Rxn<TTsVoice> selectedTtsVoice = Rxn();
  final RxString selectedLanguage = "".obs;
  final RxString ttsSpeed = "normal".obs;
  final RxInt ttsPitch = 0.obs;
  final RxString asrSpeed = "normal".obs;
  final RxString memoryType = "SHORT_TERM".obs;
  final List<String> selectedMcpEndpoints = [];

  TTsList? ttsData;
  RxList<TTsVoice> ttsList = RxList([]);

  RxList<String> languageList = RxList([]);

  RxList<ModelData> modelList = RxList([]);
  RxList<CommonMcpTool> commonMcpTools = RxList([]);

  final List<String> speedList = ["slow", "normal", "fast"];
  final List<int> pitchList = [-2, -1, 0, 1, 2];
  final List<String> memoryTypeList = ["OFF", "SHORT_TERM"];

  Future<void> initPageData(bool isWelCome) async {
    agentNameController = TextEditingController();
    assistantNameController = TextEditingController();
    characterController = TextEditingController();
    memoryController = TextEditingController();
    ever(selectedLanguage, (lang) => _updateTtsVoiceList(lang));
    await loadCommonMcpTools();
    await loadTtsList();
    await loadModelList();
    final devices = await XiaoZhiUtil.shared.getDevice(
      AppState.shared.deviceMac,
    );

    if (devices.isNotEmpty) {
      if (devices.first.agent_id != null) {
        if (isWelCome) {
          final originallyAgent = await XiaoZhiUtil.shared.getAgentDetail(
            devices.first.agent_id!,
          );

          //fromtemplateGet
          final agentTemplatesList = await XiaoZhiUtil.shared
              .agentTemplatesList(1, 10);

          if (agentTemplatesList.isNotEmpty) {
            final template = agentTemplatesList.first;
            agent = Agent(
              id: originallyAgent?.id,
              user_name: template.user_name,
              agent_name: template.agent_name,
              assistant_name: template.assistant_name,
              llm_model: template.llm_model,
              tts_voice: getTtsVoice("en", template.tts_voices ?? []),
              tts_speech_speed: template.tts_speech_speed,
              tts_pitch: template.tts_pitch,
              asr_speed: template.asr_speed,
              language: "en",
              character: template.character,
              memory: "",
              memory_type: "SHORT_TERM",
              knowledge_base_ids: template.knowledge_base_ids,
            );
            fillEditData(agent!);
          } else {
            agent = originallyAgent;
            fillEditData(agent!);
          }
        } else {
          //update
          agent = await XiaoZhiUtil.shared.getAgentDetail(
            devices.first.agent_id!,
          );
          fillEditData(agent!);
        }
      } else {
        ///没hasactivateagent,Needactivateagent
        //2. generatelicense
        final generateLicense = await XiaoZhiUtil.shared.generateLicense(
          AppState.shared.deviceMac,
        );
        if (generateLicense == null || generateLicense.serialNumber == null) {
          debugPrint("❌ Failed to generate device license.");
          AppState.shared.showToast("Failed to generate device license.");
          return;
        }

        //3. activatedevice
        final serialNumber = generateLicense.serialNumber!;
        final mac = MacAddressValidator.formatMac(AppState.shared.deviceMac);
        if (mac == null) {
          AppState.shared.showToast("Failed to format device MAC address.");
          return;
        }
        bool activateResult = await XiaoZhiUtil.shared.agentsDevicesActivate(
          serialNumber,
          mac,
        );
        if (!activateResult) {
          debugPrint(" Device云Side / EndActivateFailed");
          AppState.shared.showToast("Device cloud activation failed.");
          return;
        }

        //activatedevicesuccess
        initPageData(isWelCome);
      }
    } else {
      setDefaultCreateData();
    }
    update();
  }

  String getTtsVoice(String language, List<String> ttsVoices) {
    final String prefix = '$language:';
    for (final String voice in ttsVoices) {
      if (voice.startsWith(prefix)) {
        return voice.substring(prefix.length);
      }
    }
    if (ttsVoices.isNotEmpty) {
      for (final String voice in ttsVoices) {
        final int idx = voice.indexOf(':');
        if (idx != -1) {
          return voice.substring(idx + 1);
        }
      }
      return ttsVoices[0];
    }
    return '';
  }

  Future<void> loadTtsList() async {
    ttsData = await XiaoZhiUtil.shared.getTtsList();
    if (ttsData?.ttsVoices != null) {
      languageList.value = ttsData!.ttsVoices!.keys.toList();
    }
    if (languageList.isNotEmpty && selectedLanguage.isEmpty) {
      selectedLanguage.value = languageList.first;
    }
    _updateTtsVoiceList(selectedLanguage.value);
  }

  void _updateTtsVoiceList(String lang) {
    if (ttsData?.ttsVoices == null || lang.isEmpty) {
      ttsList.clear();
      selectedTtsVoice.value = null;
      return;
    }
    ttsList.value = ttsData!.ttsVoices![lang] ?? [];
    selectedTtsVoice.value = ttsList.isNotEmpty ? ttsList.first : null;
    update();
  }

  Future<void> loadModelList() async {
    final models = await XiaoZhiUtil.shared.getModelList();
    modelList.assignAll(models);
    update();
  }

  Future<void> loadCommonMcpTools() async {
    commonMcpTools.value = await XiaoZhiUtil.shared.getCommonMcpTool();
    update();
  }

  String getContext(String? context, String defaultString) {
    if (context == null) {
      return defaultString;
    } else if (context.isEmpty) {
      return defaultString;
    } else {
      return context;
    }
  }

  void fillEditData(Agent agent) {
    agentNameController.text = getContext(
      agent.agent_name,
      "StackChan AI Agent",
    );
    assistantNameController.text = getContext(
      agent.assistant_name,
      "StackChan",
    );
    characterController.text = agent.character ?? "";
    memoryController.text = agent.memory ?? "";
    if (languageList.contains(agent.language)) {
      selectedLanguage.value = agent.language!;
    } else if (languageList.isNotEmpty) {
      selectedLanguage.value = languageList.first;
    }

    ttsSpeed.value = agent.tts_speech_speed ?? "normal";
    ttsPitch.value = agent.tts_pitch ?? 0;
    asrSpeed.value = agent.asr_speed ?? "normal";
    memoryType.value = agent.memory_type ?? "SHORT_TERM";

    if (agent.llm_model != null && agent.llm_model!.isNotEmpty) {
      selectedModel.value = modelList.firstWhereOrNull(
        (m) => m.name == agent.llm_model,
      );
    } else {
      for (var i in modelList) {
        if (i.name != null) {
          if (i.name!.toLowerCase() == "qwen") {
            selectedModel.value = i;
            update();
            break;
          } else if (i.name!.toLowerCase().contains("qwen")) {
            selectedModel.value = i;
            update();
            break;
          }
        }
      }
    }

    if (agent.tts_voice != null) {
      Future.delayed(const Duration(milliseconds: 100), () {
        selectedTtsVoice.value = ttsList.firstWhereOrNull(
          (t) => t.voiceId == agent.tts_voice,
        );
        update();
      });
    }

    if (agent.mcp_endpoints != null) {
      selectedMcpEndpoints.addAll(agent.mcp_endpoints!);
    }
    update();
  }

  void setDefaultCreateData() {
    agentNameController.text = "My AI Agent";
    assistantNameController.text = "StackChan";
    ttsSpeed.value = "normal";
    ttsPitch.value = 0;
    asrSpeed.value = "normal";
    memoryType.value = "SHORT_TERM";
    if (modelList.isNotEmpty) selectedModel.value = modelList.first;
    update();
  }

  void toggleMcpTool(String? endpointId) {
    if (endpointId == null) return;
    selectedMcpEndpoints.contains(endpointId)
        ? selectedMcpEndpoints.remove(endpointId)
        : selectedMcpEndpoints.add(endpointId);
    update();
  }

  Future<bool> submitAgent() async {
    if (assistantNameController.text.trim().isEmpty) {
      AppState.shared.showToast("Please input assistant assistant name.");
      return false;
    }
    if (selectedModel.value == null) {
      AppState.shared.showToast("Please select an LLM Model.");
      return false;
    }
    if (selectedTtsVoice.value == null) {
      AppState.shared.showToast("Please select a voice tone.");
      return false;
    }
    isLoading.value = true;
    final agentParams = AgentCreate(
      agent_name: agent?.agent_name ?? "StackChan AI Agent",
      assistant_name: assistantNameController.text.trim(),
      llm_model: selectedModel.value!.name!,
      tts_voice: selectedTtsVoice.value!.voiceId!,
      tts_speech_speed: ttsSpeed.value,
      tts_pitch: ttsPitch.value,
      asr_speed: asrSpeed.value,
      language: selectedLanguage.value,
      character: characterController.text.trim(),
      memory: memoryController.text.trim(),
      memory_type: memoryType.value,
      mcp_endpoints: null,
      product_mcp_endpoints: null,
    );
    bool result = false;
    if (agent != null) {
      result = await XiaoZhiUtil.shared.updateAgent(agent!.id!, agentParams);
    } else {
      final agentId = await XiaoZhiUtil.shared.createAgent(agentParams);
      result = agentId != null;
    }

    isLoading.value = false;
    if (result) {
      AppState.shared.showToast(
        agent != null
            ? "Agent edited successfully"
            : "Agent created successfully",
      );
    }
    return result;
  }

  @override
  void onClose() {
    agentNameController.dispose();
    assistantNameController.dispose();
    characterController.dispose();
    memoryController.dispose();
    super.onClose();
  }
}

class _XiaoZhiWelcomePageState extends State<XiaoZhiWelcomePage> {
  String getLanguagesTitle(String lg) {
    if (ValueConstant.languages[lg] != null) {
      return ValueConstant.languages[lg]!;
    } else {
      return lg;
    }
  }

  String getSpeedText(String speed) {
    switch (speed) {
      case "slow":
        return "Slow";
      case "fast":
        return "Fast";
      default:
        return "Normal";
    }
  }

  String getMemoryText(String type) {
    switch (type) {
      case "OFF":
        return "Off";
      case "SHORT_TERM":
        return "Short Term";
      default:
        return type;
    }
  }

  XiaoZhiEditAgentModel model = XiaoZhiEditAgentModel();

  @override
  void initState() {
    super.initState();
    init();
  }

  Future<void> init() async {
    model.initPageData(widget.isWelCome ?? false);
    setState(() {});
    model.ttsList.listen((list) {
      setState(() {});
    });
    model.selectedTtsVoice.listen((data) {
      setState(() {});
    });
  }

  @override
  void dispose() {
    MusicUtil.shared.stopMusic();
    model.onClose();
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
      child: GestureDetector(
        onTap: () {
          if (mounted) {
            FocusScope.of(context).unfocus();
          }
        },
        behavior: .opaque,
        child: Column(
          children: [
            Expanded(
              child: CustomScrollView(
                slivers: [
                  CupertinoSliverNavigationBar(
                    largeTitle: Text("Agent Setting"),
                    trailing: widget.isWelCome != true
                        ? CupertinoButton(
                            padding: .zero,
                            child: Icon(
                              CupertinoIcons.xmark_circle_fill,
                              size: 25,
                              color: CupertinoColors.separator.resolveFrom(
                                context,
                              ),
                            ),
                            onPressed: () {
                              CupertinoSheetRoute.popSheet(context);
                            },
                          )
                        : SizedBox.shrink(),
                  ),
                  SliverList.list(
                    children: [
                      Image.asset(
                        "assets/lateral_image.png",
                        width: double.infinity,
                        height: 120,
                        fit: BoxFit.contain,
                      ),
                      SizedBox(height: 15),

                      //基础info
                      CupertinoListSection.insetGrouped(
                        header: Row(
                          mainAxisSize: .min,
                          spacing: 10,
                          children: [
                            Text("Assistant Name"),
                            Text(
                              "This is what Agent calls itself",
                              style: TextStyle(
                                fontSize: 10,
                                color: CupertinoColors.separator.resolveFrom(
                                  context,
                                ),
                              ),
                            ),
                          ],
                        ),
                        children: [
                          CupertinoListTile(
                            padding: .only(left: 10, right: 10),
                            title: CupertinoTextField(
                              maxLength: 30,
                              decoration: BoxDecoration(),
                              controller: model.assistantNameController,
                              placeholder: "Enter assistant name",
                              clearButtonMode: OverlayVisibilityMode.editing,
                            ),
                          ),
                        ],
                      ),

                      //languageset
                      CupertinoListSection.insetGrouped(
                        header: Text("Language"),
                        children: [
                          Obx(
                            () => buildSelectItem(
                              title: getLanguagesTitle(
                                model.selectedLanguage.value,
                              ),
                              value: Icon(
                                CupertinoIcons.right_chevron,
                                size: 16,
                              ),
                              items: model.languageList
                                  .map((value) => getLanguagesTitle(value))
                                  .toList(),
                              onTapIndex: (index) {
                                model.selectedLanguage.value =
                                    model.languageList[index];
                              },
                              selectedValue: getLanguagesTitle(
                                model.selectedLanguage.value,
                              ),
                            ),
                          ),
                        ],
                      ),

                      //LLMmodel
                      CupertinoListSection.insetGrouped(
                        header: Text("LLM Model"),
                        children: [
                          Obx(
                            () => buildSelectItem(
                              title:
                                  model.selectedModel.value?.description
                                      .regularExpressionSubstitution() ??
                                  "Select model",
                              value: Icon(
                                CupertinoIcons.right_chevron,
                                size: 16,
                              ),
                              items: model.modelList
                                  .map(
                                    (e) =>
                                        e.description
                                            .regularExpressionSubstitution() ??
                                        "",
                                  )
                                  .toList(),
                              onTapIndex: (index) {
                                model.selectedModel.value =
                                    model.modelList[index];
                              },
                              selectedValue: model
                                  .selectedModel
                                  .value
                                  ?.description
                                  .regularExpressionSubstitution(),
                            ),
                          ),
                        ],
                      ),
                      //TTSset
                      CupertinoListSection.insetGrouped(
                        header: Text("Voice Settings"),
                        children: [
                          //voice tone
                          ttsVoiceWidget(),
                          //语速
                          Obx(
                            () => buildSelectItem(
                              title: "Speed :",
                              value: Row(
                                mainAxisSize: .min,
                                spacing: 15,
                                children: [
                                  Text(getSpeedText(model.ttsSpeed.value)),
                                  Icon(CupertinoIcons.right_chevron, size: 16),
                                ],
                              ),
                              items: model.speedList
                                  .map((e) => getSpeedText(e))
                                  .toList(),
                              onTapIndex: (index) {
                                model.ttsSpeed.value = model.speedList[index];
                              },
                              selectedValue: getSpeedText(model.ttsSpeed.value),
                            ),
                          ),
                          //音调
                          Obx(
                            () => buildSelectItem(
                              title: "Pitch :",
                              value: Row(
                                mainAxisSize: .min,
                                spacing: 15,
                                children: [
                                  Text(model.ttsPitch.value.toString()),
                                  Icon(CupertinoIcons.right_chevron, size: 16),
                                ],
                              ),
                              items: model.pitchList
                                  .map((e) => e.toString())
                                  .toList(),
                              onTapIndex: (index) {
                                model.ttsPitch.value = model.pitchList[index];
                              },
                              selectedValue: model.ttsPitch.value.toString(),
                            ),
                          ),
                          //ASR语速
                          Obx(
                            () => buildSelectItem(
                              title: "ASR Speed :",
                              value: Row(
                                mainAxisSize: .min,
                                spacing: 15,
                                children: [
                                  Text(getSpeedText(model.asrSpeed.value)),
                                  Icon(CupertinoIcons.right_chevron, size: 16),
                                ],
                              ),
                              items: model.speedList
                                  .map((e) => getSpeedText(e))
                                  .toList(),
                              onTapIndex: (index) {
                                model.asrSpeed.value = model.speedList[index];
                              },
                              selectedValue: getSpeedText(model.asrSpeed.value),
                            ),
                          ),
                        ],
                      ),

                      //人设with记忆
                      CupertinoListSection.insetGrouped(
                        header: Text("Personality"),
                        children: [
                          CupertinoListTile(
                            padding: .all(15),
                            title: CupertinoTextField(
                              padding: .all(15),
                              controller: model.characterController,
                              placeholder: "Set agent character",
                              maxLines: 10,
                              clearButtonMode: OverlayVisibilityMode.editing,
                              decoration: BoxDecoration(
                                color: CupertinoColors.systemGroupedBackground
                                    .resolveFrom(context),
                                borderRadius: .circular(15),
                              ),
                            ),
                          ),
                        ],
                      ),
                      CupertinoListSection.insetGrouped(
                        header: Text("Memory"),
                        children: [
                          Obx(
                            () => buildSelectItem(
                              title: "",
                              value: Row(
                                mainAxisSize: .min,
                                spacing: 15,
                                children: [
                                  Text(getMemoryText(model.memoryType.value)),
                                  Icon(CupertinoIcons.right_chevron, size: 16),
                                ],
                              ),
                              items: model.memoryTypeList
                                  .map((e) => getMemoryText(e))
                                  .toList(),
                              onTapIndex: (index) {
                                model.memoryType.value =
                                    model.memoryTypeList[index];
                              },
                              selectedValue: getMemoryText(
                                model.memoryType.value,
                              ),
                            ),
                          ),
                          CupertinoListTile(
                            padding: .all(15),
                            title: CupertinoTextField(
                              controller: model.memoryController,
                              placeholder: "Set memory content",
                              maxLines: 10,
                              padding: .all(15),
                              decoration: BoxDecoration(
                                color: CupertinoColors.systemGroupedBackground
                                    .resolveFrom(context),
                                borderRadius: .circular(15),
                              ),
                              clearButtonMode: OverlayVisibilityMode.editing,
                            ),
                          ),
                        ],
                      ),

                      // MCP Tools
                      // CupertinoListSection.insetGrouped(
                      //   header: Text("MCP Tools"),
                      //   children: model.commonMcpTools.isNotEmpty
                      //       ? model.commonMcpTools.map((tool) {
                      //           final isSelected = model.selectedMcpEndpoints
                      //               .contains(tool.endpoint_id);
                      //           return CupertinoListTile(
                      //             onTap: () {
                      //               model.toggleMcpTool(tool.endpoint_id);
                      //               setState(() {});
                      //             },
                      //             title: Text(tool.name ?? ""),
                      //             trailing: isSelected
                      //                 ? Icon(
                      //                     CupertinoIcons.check_mark,
                      //                     size: 20,
                      //                   )
                      //                 : null,
                      //           );
                      //         }).toList()
                      //       : [
                      //           CupertinoListTile(
                      //             title: Center(
                      //               child: Text(
                      //                 "There is currently no MCP tool",
                      //               ),
                      //             ),
                      //           ),
                      //         ],
                      // ),
                      SizedBox(height: 20),
                    ],
                  ),
                ],
              ),
            ),

            //Bottombutton
            Padding(
              padding: .only(
                left: 15,
                right: 15,
                top: 15,
                bottom: MediaQuery.paddingOf(context).bottom + 15,
              ),
              child: Obx(
                () => model.isLoading.value
                    ? CupertinoActivityIndicator(radius: 16)
                    : widget.isWelCome == true
                    ? Row(
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
                                      return DeviceWifiConfig(isWelCome: true);
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
                                final res = await model.submitAgent();
                                if (res && mounted) {
                                  Navigator.push(
                                    this.context,
                                    CupertinoPageRoute(
                                      builder: (_) =>
                                          DeviceWifiConfig(isWelCome: true),
                                    ),
                                  );
                                }
                              },
                            ),
                          ),
                        ],
                      )
                    : CupertinoButton.filled(
                        child: SizedBox(
                          width: .infinity,
                          child: Center(child: Text("Save")),
                        ),
                        onPressed: () async {
                          if (mounted) {
                            FocusScope.of(context).unfocus();
                          }
                          final result = await model.submitAgent();
                          if (result && mounted) {
                            showCupertinoDialog(
                              barrierDismissible: false,
                              context: this.context,
                              builder: (context) {
                                return CupertinoAlertDialog(
                                  title: Text(
                                    "AI agent config has been saved successfully, and will be active after manually restarting the device.",
                                  ),
                                  actions: [
                                    CupertinoDialogAction(
                                      onPressed: () {
                                        Navigator.of(context).pop();
                                        Navigator.pop(this.context);
                                      },
                                      isDefaultAction: true,
                                      child: Text("Confirm"),
                                    ),
                                  ],
                                );
                              },
                            );
                          }
                        },
                      ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget ttsVoiceWidget() {
    return PullDownButton(
      useRootNavigator: true,
      itemBuilder: (context) => model.ttsList
          .map(
            (e) => PullDownMenuItem.selectable(
              onTap: () {
                model.selectedTtsVoice.value = e;
              },
              title: e.voiceName ?? "",
              selected: model.selectedTtsVoice.value?.voiceId == e.voiceId,
              iconWidget: e.voiceDemo != null
                  ? CupertinoButton(
                      padding: .zero,
                      child: Icon(CupertinoIcons.speaker_2),
                      onPressed: () {
                        MusicUtil.shared.playUrlMusicOnce(
                          e.voiceDemo,
                          completion: () {
                            setState(() {});
                          },
                        );
                      },
                    )
                  : null,
            ),
          )
          .toList(),
      buttonBuilder: (context, showMenu) => CupertinoButton(
        padding: EdgeInsets.zero,
        foregroundColor: CupertinoColors.systemGrey.resolveFrom(context),
        minimumSize: .zero,
        onPressed: showMenu,
        pressedOpacity: 1,
        child: CupertinoListTile(
          title: Text("Voice :"),
          trailing: Row(
            mainAxisSize: .min,
            spacing: 15,
            children: [
              Text(model.selectedTtsVoice.value?.voiceName ?? "Select voice"),
              Icon(CupertinoIcons.right_chevron, size: 16),
            ],
          ),
          leading: null,
        ),
      ),
    );
  }

  Widget buildSelectItem({
    required String title,
    required Widget value,
    required List<String> items,
    required Function(int) onTapIndex,
    required String? selectedValue,
  }) {
    return PullDownButton(
      useRootNavigator: true,
      itemBuilder: (context) => items.indexed
          .map(
            (item) => PullDownMenuItem.selectable(
              onTap: () {
                onTapIndex(item.$1);
              },
              title: item.$2,
              selected: item.$2 == selectedValue,
            ),
          )
          .toList(),
      buttonBuilder: (context, showMenu) {
        return CupertinoButton(
          padding: EdgeInsets.zero,
          foregroundColor: CupertinoColors.systemGrey.resolveFrom(context),
          minimumSize: .zero,
          onPressed: showMenu,
          pressedOpacity: 1,
          child: CupertinoListTile(
            title: Text(title),
            trailing: value,
            leading: null,
          ),
        );
      },
    );
  }
}

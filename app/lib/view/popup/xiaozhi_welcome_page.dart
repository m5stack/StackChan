/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/theme_setting.dart';
import 'package:get/get.dart';
import 'package:stack_chan/model/XiaoZhi/agent.dart';
import 'package:stack_chan/util/extension.dart';
import 'package:stack_chan/util/music_util.dart';
import 'package:stack_chan/view/popup/device_wifi_config.dart';

import '../../app_state.dart';
import '../../model/XiaoZhi/XiaoZhi_model.dart';
import '../../model/XiaoZhi/agent_create.dart';
import '../../model/XiaoZhi/common_mcp_tool.dart';
import '../../model/XiaoZhi/tts_list.dart';
import '../../util/mac_address_validator.dart';
import '../../util/xiao_zhi_util.dart';
import '../../util/value_constant.dart';

class XiaoZhiWelcomePage extends StatefulWidget {
  const XiaoZhiWelcomePage({super.key, this.isWelCome});

  final bool? isWelCome;

  @override
  State<StatefulWidget> createState() => _XiaoZhiWelcomePageState();
}

class XiaoZhiEditAgentModel extends GetxController {
  static const String _defaultAgentName = "StackChan AI Agent";

  Agent? agent;

  final RxBool isLoading = false.obs;

  late TextEditingController agentNameController;
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
    characterController = TextEditingController();
    memoryController = TextEditingController();
    ever(selectedLanguage, (lang) => _updateTtsVoiceList(lang));
    await loadCommonMcpTools();
    await loadTtsList();
    await loadModelList();
    final isFirstBoundDevice = await _isFirstBoundDevice(isWelCome);
    final devices = await XiaoZhiUtil.shared.getDevice(
      AppState.shared.deviceMac,
    );

    if (devices.isNotEmpty) {
      if (devices.first.agentId != null) {
        if (isWelCome) {
          final originallyAgent = await XiaoZhiUtil.shared.getAgentDetail(
            devices.first.agentId!,
          );

          // Fetch agent templates to initialize a new agent.
          final agentTemplatesList = await XiaoZhiUtil.shared
              .agentTemplatesList(1, 10);

          if (agentTemplatesList.isNotEmpty) {
            final template = agentTemplatesList.first;
            agent = Agent(
              id: originallyAgent?.id,
              userName: template.userName,
              agentName: getContext(template.agentName, _defaultAgentName),
              llmModel: template.llmModel,
              ttsVoice: getTtsVoice("en", template.ttsVoices ?? []),
              ttsSpeechSpeed: template.ttsSpeechSpeed,
              ttsPitch: template.ttsPitch,
              asrSpeed: template.asrSpeed,
              language: "en",
              character: template.character,
              memory: "",
              memoryType: "SHORT_TERM",
              knowledgeBaseIds: template.knowledgeBaseIds,
            );
            fillEditData(agent!, preferQwen: isFirstBoundDevice);
          } else {
            agent = originallyAgent;
            if (agent != null) {
              fillEditData(agent!, preferQwen: isFirstBoundDevice);
            } else {
              setDefaultCreateData(preferQwen: isFirstBoundDevice);
            }
          }
        } else {
          // Refresh the existing agent's details.
          agent = await XiaoZhiUtil.shared.getAgentDetail(
            devices.first.agentId!,
          );
          if (agent != null) {
            fillEditData(agent!);
          } else {
            setDefaultCreateData();
          }
        }
      } else {
        setDefaultCreateData(preferQwen: isFirstBoundDevice);
      }
    } else {
      setDefaultCreateData(preferQwen: isFirstBoundDevice);
    }
    update();
  }

  Future<bool> _isFirstBoundDevice(bool isWelCome) async {
    if (!isWelCome) return false;
    await AppState.shared.getDevices(force: true);
    final boundDevices = AppState.shared.devices;
    if (boundDevices.length != 1) return false;
    return MacAddressValidator.areEqual(
      boundDevices.first.mac,
      AppState.shared.deviceMac,
    );
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
    _selectDefaultTtsVoice(preferredLanguage: selectedLanguage.value);
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

  /// Selects a voice for page initialization when no voice has been chosen.
  /// The order is the current language, English, then the first available
  /// voice in the complete TTS map.
  void _selectDefaultTtsVoice({String? preferredLanguage, String? voiceId}) {
    final voicesByLanguage = ttsData?.ttsVoices;
    if (voicesByLanguage == null || voicesByLanguage.isEmpty) {
      ttsList.clear();
      selectedTtsVoice.value = null;
      return;
    }

    String? resolvedLanguage;
    TTsVoice? resolvedVoice;

    final preferredVoices = _voicesForLanguage(
      voicesByLanguage,
      preferredLanguage,
    );
    if (voiceId != null && voiceId.isNotEmpty) {
      resolvedVoice = preferredVoices.firstWhereOrNull(
        (voice) => voice.voiceId == voiceId,
      );
      if (resolvedVoice == null) {
        for (final entry in voicesByLanguage.entries) {
          resolvedVoice = entry.value.firstWhereOrNull(
            (voice) => voice.voiceId == voiceId,
          );
          if (resolvedVoice != null) {
            resolvedLanguage = entry.key;
            break;
          }
        }
      }
    }
    if (resolvedVoice != null) {
      resolvedLanguage = _languageForVoice(voicesByLanguage, resolvedVoice);
    } else if (preferredVoices.isNotEmpty) {
      resolvedLanguage = _languageKey(voicesByLanguage, preferredLanguage);
      resolvedVoice = preferredVoices.first;
    }

    if (resolvedVoice == null) {
      final englishKey = _languageKey(voicesByLanguage, "en");
      final englishVoices = _voicesForLanguage(voicesByLanguage, englishKey);
      if (englishVoices.isNotEmpty) {
        resolvedLanguage = englishKey;
        resolvedVoice = englishVoices.first;
      }
    }

    if (resolvedVoice == null) {
      for (final entry in voicesByLanguage.entries) {
        if (entry.value.isNotEmpty) {
          resolvedLanguage = entry.key;
          resolvedVoice = entry.value.first;
          break;
        }
      }
    }

    if (resolvedVoice == null) {
      ttsList.clear();
      selectedTtsVoice.value = null;
      return;
    }

    if (resolvedLanguage != null &&
        selectedLanguage.value != resolvedLanguage) {
      selectedLanguage.value = resolvedLanguage;
    }
    ttsList.value = voicesByLanguage[resolvedLanguage] ?? [resolvedVoice];
    selectedTtsVoice.value = resolvedVoice;
    update();
  }

  List<TTsVoice> _voicesForLanguage(
    Map<String, List<TTsVoice>> voicesByLanguage,
    String? language,
  ) {
    final key = _languageKey(voicesByLanguage, language);
    return key == null ? const [] : voicesByLanguage[key] ?? const [];
  }

  String? _languageKey(
    Map<String, List<TTsVoice>> voicesByLanguage,
    String? language,
  ) {
    if (language == null || language.isEmpty) return null;
    for (final key in voicesByLanguage.keys) {
      if (key == language || key.toLowerCase() == language.toLowerCase()) {
        return key;
      }
    }
    return null;
  }

  String? _languageForVoice(
    Map<String, List<TTsVoice>> voicesByLanguage,
    TTsVoice voice,
  ) {
    for (final entry in voicesByLanguage.entries) {
      if (entry.value.contains(voice)) return entry.key;
    }
    return null;
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

  void fillEditData(Agent agent, {bool preferQwen = false}) {
    agentNameController.text = getContext(agent.agentName, _defaultAgentName);
    characterController.text = agent.character ?? "";
    memoryController.text = agent.memory ?? "";
    final preferredLanguage = languageList.contains(agent.language)
        ? agent.language
        : null;
    if (preferredLanguage != null) {
      selectedLanguage.value = preferredLanguage;
    }

    ttsSpeed.value = agent.ttsSpeechSpeed ?? "normal";
    ttsPitch.value = agent.ttsPitch ?? 0;
    asrSpeed.value = agent.asrSpeed ?? "normal";
    memoryType.value = agent.memoryType ?? "SHORT_TERM";

    selectedModel.value = _selectModel(agent.llmModel, preferQwen: preferQwen);

    _selectDefaultTtsVoice(
      preferredLanguage: preferredLanguage,
      voiceId: agent.ttsVoice,
    );

    if (agent.mcpEndpoints != null) {
      selectedMcpEndpoints.addAll(agent.mcpEndpoints!);
    }
    update();
  }

  ModelData? _selectModel(String? modelName, {bool preferQwen = false}) {
    if (preferQwen) {
      final qwenModel = _findQwenModel();
      if (qwenModel != null) return qwenModel;
    }

    if (modelName != null && modelName.isNotEmpty) {
      final matchedModel = modelList.firstWhereOrNull(
        (m) => m.name == modelName,
      );
      if (matchedModel != null) return matchedModel;
    }

    return _findQwenModel() ?? (modelList.isNotEmpty ? modelList.first : null);
  }

  ModelData? _findQwenModel() {
    ModelData? fallback;
    for (final model in modelList) {
      final name = model.name?.toLowerCase() ?? "";
      final description = model.description?.toLowerCase() ?? "";
      if (name == "qwen") return model;
      if (fallback == null &&
          (name.contains("qwen") || description.contains("qwen"))) {
        fallback = model;
      }
    }
    return fallback;
  }

  void setDefaultCreateData({bool preferQwen = false}) {
    agentNameController.text = "My AI Agent";
    ttsSpeed.value = "normal";
    ttsPitch.value = 0;
    asrSpeed.value = "normal";
    memoryType.value = "SHORT_TERM";
    selectedModel.value = _selectModel(null, preferQwen: preferQwen);
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
    if (selectedModel.value == null) {
      AppState.shared.showToast("Please select an LLM Model.");
      return false;
    }
    if (selectedTtsVoice.value == null) {
      AppState.shared.showToast("Please select a voice tone.");
      return false;
    }
    isLoading.value = true;
    try {
      final agentParams = AgentCreate(
        agentName: agent?.agentName ?? _defaultAgentName,
        llmModel: selectedModel.value!.name!,
        ttsVoice: selectedTtsVoice.value!.voiceId!,
        ttsSpeechSpeed: ttsSpeed.value,
        ttsPitch: ttsPitch.value,
        asrSpeed: asrSpeed.value,
        language: selectedLanguage.value,
        character: characterController.text.trim(),
        memory: memoryController.text.trim(),
        memoryType: memoryType.value,
        mcpEndpoints: null,
        productMcpEndpoints: null,
      );
      bool result = false;
      if (agent != null) {
        result = await XiaoZhiUtil.shared.updateAgent(agent!.id!, agentParams);
      } else {
        final agentId = await XiaoZhiUtil.shared.createAgent(agentParams);
        result = agentId != null;
      }

      if (result) {
        AppState.shared.showToast(
          agent != null
              ? "Agent edited successfully"
              : "Agent created successfully",
        );
      } else {
        AppState.shared.showToast(
          agent != null ? "Failed to edit agent" : "Failed to create agent",
        );
      }
      return result;
    } catch (e) {
      debugPrint("submitAgent error: $e");
      AppState.shared.showToast(
        agent != null ? "Failed to edit agent" : "Failed to create agent",
      );
      return false;
    } finally {
      isLoading.value = false;
    }
  }

  @override
  void onClose() {
    agentNameController.dispose();
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
    FocusManager.instance.primaryFocus?.unfocus();
    MusicUtil.shared.stopMusic();
    model.onClose();
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
                  leading: widget.isWelCome != true
                      ? const SizedBox.shrink()
                      : null,
                  largeTitle: const Text("Agent Setting"),
                  stretch: true,
                  trailing: widget.isWelCome != true
                      ? CupertinoButton(
                          sizeStyle: .medium,
                          onPressed: () {
                            Navigator.maybePop(context);
                          },
                          child: const Icon(CupertinoIcons.xmark),
                        )
                      : const SizedBox.shrink(),
                ),
                SliverPadding(
                  padding: .only(left: padding.left, right: padding.right),
                  sliver: SliverList.list(
                    children: [
                      Image.asset(
                        "assets/lateral_image.png",
                        width: double.infinity,
                        height: 120,
                        fit: BoxFit.contain,
                      ),
                      const SizedBox(height: 15),

                      CupertinoListSection.insetGrouped(
                        header: const Text("Language"),
                        children: [
                          Obx(
                            () => buildSelectItem(
                              context,
                              title: getLanguagesTitle(
                                model.selectedLanguage.value,
                              ),
                              value: const Icon(
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

                      // Language
                      // CupertinoListSection.insetGrouped(
                      //   header: Text("Language"),
                      //   children: [
                      //     Obx(
                      //       () => buildSelectItem(
                      //         title: getLanguagesTitle(
                      //           model.selectedLanguage.value,
                      //         ),
                      //         value: Icon(
                      //           CupertinoIcons.right_chevron,
                      //           size: 16,
                      //         ),
                      //         items: model.languageList
                      //             .map((value) => getLanguagesTitle(value))
                      //             .toList(),
                      //         onTapIndex: (index) {
                      //           model.selectedLanguage.value =
                      //               model.languageList[index];
                      //         },
                      //         selectedValue: getLanguagesTitle(
                      //           model.selectedLanguage.value,
                      //         ),
                      //       ),
                      //     ),
                      //   ],
                      // ),
                      CupertinoListSection.insetGrouped(
                        header: const Text("LLM Model"),
                        children: [
                          Obx(
                            () => buildSelectItem(
                              context,
                              title:
                                  model.selectedModel.value?.description
                                      .regularExpressionSubstitution() ??
                                  "Select model",
                              value: const Icon(
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

                      // LLM model
                      // CupertinoListSection.insetGrouped(
                      //   header: Text("LLM Model"),
                      //   children: [
                      //     Obx(
                      //       () => buildSelectItem(
                      //         title:
                      //             model.selectedModel.value?.description
                      //                 .regularExpressionSubstitution() ??
                      //             "Select model",
                      //         value: Icon(
                      //           CupertinoIcons.right_chevron,
                      //           size: 16,
                      //         ),
                      //         items: model.modelList
                      //             .map(
                      //               (e) =>
                      //                   e.description
                      //                       .regularExpressionSubstitution() ??
                      //                   "",
                      //             )
                      //             .toList(),
                      //         onTapIndex: (index) {
                      //           model.selectedModel.value =
                      //               model.modelList[index];
                      //         },
                      //         selectedValue: model
                      //             .selectedModel
                      //             .value
                      //             ?.description
                      //             .regularExpressionSubstitution(),
                      //       ),
                      //     ),
                      //   ],
                      // ),
                      CupertinoListSection.insetGrouped(
                        header: const Text("Voice Settings"),
                        children: [
                          ttsVoiceWidget(context),
                          Obx(
                            () => buildSelectItem(
                              context,
                              title: "Speed :",
                              value: Row(
                                mainAxisSize: .min,
                                spacing: 15,
                                children: [
                                  Text(getSpeedText(model.ttsSpeed.value)),
                                  const Icon(
                                    CupertinoIcons.right_chevron,
                                    size: 16,
                                  ),
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
                          Obx(
                            () => buildSelectItem(
                              context,
                              title: "Pitch :",
                              value: Row(
                                mainAxisSize: .min,
                                spacing: 15,
                                children: [
                                  Text(model.ttsPitch.value.toString()),
                                  const Icon(
                                    CupertinoIcons.right_chevron,
                                    size: 16,
                                  ),
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
                          Obx(
                            () => buildSelectItem(
                              context,
                              title: "ASR Speed :",
                              value: Row(
                                mainAxisSize: .min,
                                spacing: 15,
                                children: [
                                  Text(getSpeedText(model.asrSpeed.value)),
                                  const Icon(
                                    CupertinoIcons.right_chevron,
                                    size: 16,
                                  ),
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

                      // TTS settings
                      // CupertinoListSection.insetGrouped(
                      //   header: Text("Voice Settings"),
                      //   children: [
                      //     // Voice tone
                      //     ttsVoiceWidget(),
                      //     // Speech speed
                      //     Obx(
                      //       () => buildSelectItem(
                      //         title: "Speed :",
                      //         value: Row(
                      //           mainAxisSize: .min,
                      //           spacing: 15,
                      //           children: [
                      //             Text(getSpeedText(model.ttsSpeed.value)),
                      //             Icon(CupertinoIcons.right_chevron, size: 16),
                      //           ],
                      //         ),
                      //         items: model.speedList
                      //             .map((e) => getSpeedText(e))
                      //             .toList(),
                      //         onTapIndex: (index) {
                      //           model.ttsSpeed.value = model.speedList[index];
                      //         },
                      //         selectedValue: getSpeedText(model.ttsSpeed.value),
                      //       ),
                      //     ),
                      //     // Pitch
                      //     Obx(
                      //       () => buildSelectItem(
                      //         title: "Pitch :",
                      //         value: Row(
                      //           mainAxisSize: .min,
                      //           spacing: 15,
                      //           children: [
                      //             Text(model.ttsPitch.value.toString()),
                      //             Icon(CupertinoIcons.right_chevron, size: 16),
                      //           ],
                      //         ),
                      //         items: model.pitchList
                      //             .map((e) => e.toString())
                      //             .toList(),
                      //         onTapIndex: (index) {
                      //           model.ttsPitch.value = model.pitchList[index];
                      //         },
                      //         selectedValue: model.ttsPitch.value.toString(),
                      //       ),
                      //     ),
                      //     // ASR speed
                      //     Obx(
                      //       () => buildSelectItem(
                      //         title: "ASR Speed :",
                      //         value: Row(
                      //           mainAxisSize: .min,
                      //           spacing: 15,
                      //           children: [
                      //             Text(getSpeedText(model.asrSpeed.value)),
                      //             Icon(CupertinoIcons.right_chevron, size: 16),
                      //           ],
                      //         ),
                      //         items: model.speedList
                      //             .map((e) => getSpeedText(e))
                      //             .toList(),
                      //         onTapIndex: (index) {
                      //           model.asrSpeed.value = model.speedList[index];
                      //         },
                      //         selectedValue: getSpeedText(model.asrSpeed.value),
                      //       ),
                      //     ),
                      //   ],
                      // ),
                      CupertinoListSection.insetGrouped(
                        header: const Text("Personality"),
                        children: [
                          CupertinoListTile(
                            padding: const .all(15),
                            title: CupertinoTextField(
                              padding: const .all(15),
                              enableInlinePrediction: true,
                              enableSuggestions: true,
                              controller: model.characterController,
                              placeholder: "Set agent character",
                              maxLines: 10,
                              clearButtonMode: OverlayVisibilityMode.editing,
                              decoration: BoxDecoration(
                                color: CupertinoColors.tertiarySystemFill
                                    .resolveFrom(context),
                                borderRadius: .circular(10),
                              ),
                            ),
                          ),
                        ],
                      ),

                      // Character and memory
                      // CupertinoListSection.insetGrouped(
                      //   header: Text("Personality"),
                      //   children: [
                      //     CupertinoListTile(
                      //       padding: .all(15),
                      //       title: CupertinoTextField(
                      //         padding: .all(15),
                      //         enableInlinePrediction: true,
                      //         enableSuggestions: true,
                      //         controller: model.characterController,
                      //         placeholder: "Set agent character",
                      //         maxLines: 10,
                      //         clearButtonMode: OverlayVisibilityMode.editing,
                      //         decoration: BoxDecoration(
                      //           color: CupertinoColors.systemGroupedBackground
                      //               .resolveFrom(context),
                      //           borderRadius: .circular(15),
                      //         ),
                      //       ),
                      //     ),
                      //   ],
                      // ),
                      CupertinoListSection.insetGrouped(
                        header: const Text("Memory"),
                        children: [
                          Obx(
                            () => buildSelectItem(
                              context,
                              title: "Status :",
                              value: Row(
                                mainAxisSize: .min,
                                spacing: 15,
                                children: [
                                  Text(getMemoryText(model.memoryType.value)),
                                  const Icon(
                                    CupertinoIcons.right_chevron,
                                    size: 16,
                                  ),
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
                            padding: const .all(15),
                            title: CupertinoTextField(
                              controller: model.memoryController,
                              placeholder: "Set memory content",
                              enableInlinePrediction: true,
                              maxLines: 10,
                              padding: const .all(15),
                              decoration: BoxDecoration(
                                color: CupertinoColors.tertiarySystemFill
                                    .resolveFrom(context),
                                borderRadius: .circular(10),
                              ),
                              clearButtonMode: OverlayVisibilityMode.editing,
                            ),
                          ),
                        ],
                      ),

                      // CupertinoListSection.insetGrouped(
                      //   header: Text("Memory"),
                      //   children: [
                      //     Obx(
                      //       () => buildSelectItem(
                      //         title: "Status :",
                      //         value: Row(
                      //           mainAxisSize: .min,
                      //           spacing: 15,
                      //           children: [
                      //             Text(getMemoryText(model.memoryType.value)),
                      //             Icon(CupertinoIcons.right_chevron, size: 16),
                      //           ],
                      //         ),
                      //         items: model.memoryTypeList
                      //             .map((e) => getMemoryText(e))
                      //             .toList(),
                      //         onTapIndex: (index) {
                      //           model.memoryType.value =
                      //               model.memoryTypeList[index];
                      //         },
                      //         selectedValue: getMemoryText(
                      //           model.memoryType.value,
                      //         ),
                      //       ),
                      //     ),
                      //     CupertinoListTile(
                      //       padding: .all(15),
                      //       title: CupertinoTextField(
                      //         controller: model.memoryController,
                      //         placeholder: "Set memory content",
                      //         enableInlinePrediction: true,
                      //         maxLines: 10,
                      //         padding: .all(15),
                      //         decoration: BoxDecoration(
                      //           color: CupertinoColors.systemGroupedBackground
                      //               .resolveFrom(context),
                      //           borderRadius: .circular(15),
                      //         ),
                      //         clearButtonMode: OverlayVisibilityMode.editing,
                      //       ),
                      //     ),
                      //   ],
                      // ),

                      // MCP Tools
                      // CupertinoListSection.insetGrouped(
                      //   header: Text("MCP Tools"),
                      //   children: model.commonMcpTools.isNotEmpty
                      //       ? model.commonMcpTools.map((tool) {
                      //           final isSelected = model.selectedMcpEndpoints
                      //               .contains(tool.endpointId);
                      //           return CupertinoListTile(
                      //             onTap: () {
                      //               model.toggleMcpTool(tool.endpointId);
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
                      const SizedBox(height: 20),
                    ],
                  ),
                ),
              ],
            ),
          ),

          // Bottom button
          Padding(
            padding: .only(
              left: 20 + padding.left,
              right: 20 + padding.right,
              top: 20,
              bottom: MediaQuery.paddingOf(context).bottom + 20,
            ),
            child: Obx(
              () => model.isLoading.value
                  ? const CupertinoActivityIndicator(radius: 16)
                  : widget.isWelCome == true
                  ? Row(
                      spacing: 15,
                      children: [
                        Expanded(
                          child: CupertinoButton.tinted(
                            child: Center(
                              child: Text(
                                "Skip",
                                style: TextStyle(
                                  color: CupertinoTheme.of(context)
                                      .primaryColor,
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
                                    return const DeviceWifiConfig(
                                      isWelCome: true,
                                    );
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
                              final res = await model.submitAgent();
                              if (res && mounted) {
                                Navigator.push(
                                  this.context,
                                  CupertinoPageRoute(
                                    builder: (_) =>
                                        const DeviceWifiConfig(isWelCome: true),
                                  ),
                                );
                              }
                            },
                          ),
                        ),
                      ],
                    )
                  : CupertinoButton.filled(
                      child: const Center(
                        child: Text(
                          "Save",
                          style: TextStyle(color: CupertinoColors.white),
                        ),
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
                                title: const Text(
                                  "AI agent config has been saved successfully, and will be active after manually restarting the device.",
                                ),
                                actions: [
                                  CupertinoDialogAction(
                                    onPressed: () {
                                      Navigator.maybePop(context);
                                      Navigator.maybePop(this.context);
                                    },
                                    isDefaultAction: true,
                                    child: const Text("Confirm"),
                                  ),
                                ],
                              );
                            },
                          );
                        }
                      },
                    ),

              // CupertinoButton.filled(
              //         child: SizedBox(
              //           width: .infinity,
              //           child: Center(child: Text("Save")),
              //         ),
              //         onPressed: () async {
              //           if (mounted) {
              //             FocusScope.of(context).unfocus();
              //           }
              //           final result = await model.submitAgent();
              //           if (result && mounted) {
              //             showCupertinoDialog(
              //               barrierDismissible: false,
              //               context: this.context,
              //               builder: (context) {
              //                 return CupertinoAlertDialog(
              //                   title: Text(
              //                     "AI agent config has been saved successfully, and will be active after manually restarting the device.",
              //                   ),
              //                   actions: [
              //                     CupertinoDialogAction(
              //                       onPressed: () {
              //                         Navigator.maybePop(context);
              //                         Navigator.maybePop(this.context);
              //                       },
              //                       isDefaultAction: true,
              //                       child: Text("Confirm"),
              //                     ),
              //                   ],
              //                 );
              //               },
              //             );
              //           }
              //         },
              //       ),
            ),
          ),
        ],
      ),
    );
  }

  Widget ttsVoiceWidget(BuildContext context) {
    final padding = MediaQuery.viewPaddingOf(context);
    return CupertinoMenuAnchor(
      constraints: const BoxConstraints.tightFor(width: 300),
      consumeOutsideTaps: true,
      overlayPadding: .only(
        bottom: padding.bottom,
        left: padding.left,
        right: padding.right,
        top:
            padding.top +
            (MediaQuery.sizeOf(context).height / 10) +
            kMinInteractiveDimensionCupertino,
      ),
      menuChildren: model.ttsList
          .map(
            (e) => CupertinoMenuItem(
              onPressed: () {
                model.selectedTtsVoice.value = e;
                MusicUtil.shared.playUrlMusicOnce(
                  e.voiceDemo,
                  completion: () {
                    setState(() {});
                  },
                );
              },
              leading: model.selectedTtsVoice.value?.voiceId == e.voiceId
                  ? const Icon(CupertinoIcons.check_mark)
                  : const SizedBox.shrink(),
              child: Row(
                children: [
                  Expanded(child: Text(e.voiceName ?? "")),
                  e.voiceDemo != null
                      ? const Icon(CupertinoIcons.speaker_2)
                      : const SizedBox.shrink(),
                ],
              ),
            ),
          )
          .toList(),
      builder: (context, controller, child) => CupertinoListTile(
        onTap: () {
          if (controller.isOpen) {
            controller.close();
          } else {
            controller.open();
          }
        },
        title: const Text("Voice :"),
        trailing: Row(
          mainAxisSize: .min,
          spacing: 15,
          children: [
            Text(model.selectedTtsVoice.value?.voiceName ?? "Select voice"),
            const Icon(CupertinoIcons.right_chevron, size: 16),
          ],
        ),
      ),
    );

    // return PullDownButton(
    //   useRootNavigator: false,
    //   itemBuilder: (context) {
    //     final List<PullDownMenuEntry> pullDownMenuItems = model.ttsList
    //         .map<PullDownMenuEntry>(
    //           (e) => PullDownMenuItem.selectable(
    //             onTap: () {
    //               model.selectedTtsVoice.value = e;
    //             },
    //             title: e.voiceName ?? "",
    //             selected: model.selectedTtsVoice.value?.voiceId == e.voiceId,
    //             iconWidget: e.voiceDemo != null
    //                 ? CupertinoButton(
    //                     padding: .zero,
    //                     child: Icon(CupertinoIcons.speaker_2),
    //                     onPressed: () {
    //                       MusicUtil.shared.playUrlMusicOnce(
    //                         e.voiceDemo,
    //                         completion: () {
    //                           setState(() {});
    //                         },
    //                       );
    //                     },
    //                   )
    //                 : null,
    //           ),
    //         )
    //         .toList();
    //     if (pullDownMenuItems.length > 8) {
    //       pullDownMenuItems.insert(0, PullDownMenuDivider.large());
    //       pullDownMenuItems.insert(0, PullDownMenuDivider.large());
    //       pullDownMenuItems.insert(0, PullDownMenuDivider.large());
    //       pullDownMenuItems.insert(0, PullDownMenuDivider.large());
    //       pullDownMenuItems.insert(0, PullDownMenuDivider.large());
    //       pullDownMenuItems.insert(0, PullDownMenuDivider.large());
    //       pullDownMenuItems.insert(0, PullDownMenuDivider.large());
    //     }
    //     return pullDownMenuItems;
    //   },
    //   buttonBuilder: (context, showMenu) => CupertinoButton(
    //     padding: EdgeInsets.zero,
    //     foregroundColor: CupertinoColors.systemGrey.resolveFrom(context),
    //     minimumSize: .zero,
    //     onPressed: showMenu,
    //     pressedOpacity: 1,
    //     child: CupertinoListTile(
    //       title: Text("Voice :"),
    //       trailing: Row(
    //         mainAxisSize: .min,
    //         spacing: 15,
    //         children: [
    //           Text(model.selectedTtsVoice.value?.voiceName ?? "Select voice"),
    //           Icon(CupertinoIcons.right_chevron, size: 16),
    //         ],
    //       ),
    //       leading: null,
    //     ),
    //   ),
    // );
  }

  Widget buildSelectItem(
    BuildContext context, {
    required String title,
    required Widget value,
    required List<String> items,
    required Function(int) onTapIndex,
    required String? selectedValue,
  }) {
    final padding = MediaQuery.viewPaddingOf(context);
    return CupertinoMenuAnchor(
      constraints: const BoxConstraints(maxWidth: 300),
      consumeOutsideTaps: true,
      overlayPadding: .only(
        bottom: padding.bottom,
        left: padding.left,
        right: padding.right,
        top:
            padding.top +
            (MediaQuery.sizeOf(context).height / 10) +
            kMinInteractiveDimensionCupertino,
      ),
      menuChildren: items.indexed
          .map(
            (item) => CupertinoMenuItem(
              onPressed: () {
                onTapIndex(item.$1);
              },
              leading: item.$2 == selectedValue
                  ? const Icon(CupertinoIcons.check_mark)
                  : null,
              child: Text(item.$2),
            ),
          )
          .toList(),
      builder: (context, controller, child) => CupertinoListTile(
        title: Text(title),
        trailing: value,
        onTap: () {
          if (controller.isOpen) {
            controller.close();
          } else {
            controller.open();
          }
        },
      ),
    );

    // return PullDownButton(
    //   useRootNavigator: false,
    //   itemBuilder: (context) {
    //     final List<PullDownMenuEntry> pullDownMenuItems = items.indexed
    //         .map<PullDownMenuEntry>(
    //           (item) => PullDownMenuItem.selectable(
    //             onTap: () {
    //               onTapIndex(item.$1);
    //             },
    //             title: item.$2,
    //             selected: item.$2 == selectedValue,
    //           ),
    //         )
    //         .toList();
    //     if (pullDownMenuItems.length > 8) {
    //       pullDownMenuItems.insert(0, PullDownMenuDivider.large());
    //       pullDownMenuItems.insert(0, PullDownMenuDivider.large());
    //       pullDownMenuItems.insert(0, PullDownMenuDivider.large());
    //       pullDownMenuItems.insert(0, PullDownMenuDivider.large());
    //       pullDownMenuItems.insert(0, PullDownMenuDivider.large());
    //       pullDownMenuItems.insert(0, PullDownMenuDivider.large());
    //       pullDownMenuItems.insert(0, PullDownMenuDivider.large());
    //       pullDownMenuItems.insert(0, PullDownMenuDivider.large());
    //     }
    //     return pullDownMenuItems;
    //   },
    //   buttonBuilder: (context, showMenu) {
    //     return CupertinoButton(
    //       padding: EdgeInsets.zero,
    //       foregroundColor: CupertinoColors.systemGrey.resolveFrom(context),
    //       minimumSize: .zero,
    //       onPressed: showMenu,
    //       pressedOpacity: 1,
    //       child: CupertinoListTile(
    //         title: Text(title),
    //         trailing: value,
    //         leading: null,
    //       ),
    //     );
    //   },
    // );
  }
}

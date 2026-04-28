/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';
import 'package:get/get.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/model/XiaoZhi/agent.dart';
import 'package:stack_chan/model/XiaoZhi/tts_list.dart';
import 'package:stack_chan/model/XiaoZhi/XiaoZhi_model.dart';
import 'package:stack_chan/util/value_constant.dart';

import '../../model/XiaoZhi/agent_create.dart';
import '../../model/XiaoZhi/common_mcp_tool.dart';
import '../../util/XiaoZhi_util.dart';
import 'agent_configuration.dart';

//EditorCreate agent
class EditAgent extends StatefulWidget {
  const EditAgent({super.key, this.agent});

  final Agent? agent;

  @override
  State<StatefulWidget> createState() => _EditAgentState();
}

class EditAgentModel extends GetxController {
  final Agent? agent;

  EditAgentModel(this.agent);

  final RxBool isEdit = false.obs;
  final RxBool isLoading = false.obs;

  late TextEditingController agentNameController;
  late TextEditingController assistantNameController;
  late TextEditingController characterController;
  late TextEditingController memoryController;

  final Rxn<ModelData> selectedModel = Rxn();
  final Rxn<TTsVoice> selectedTtsVoice = Rxn();
  final RxString selectedLanguage = "".obs; //，dynamic
  final RxString ttsSpeed = "normal".obs;
  final RxInt ttsPitch = 0.obs;
  final RxString asrSpeed = "normal".obs;
  final RxString memoryType = "SHORT_TERM".obs;
  final List<String> selectedMcpEndpoints = [];

  TTsList? ttsData;
  RxList<TTsVoice> ttsList = RxList([]);

  RxList<String> languageList = RxList([]);

  RxList<ModelData> modelList = RxList([]);
  List<CommonMcpTool> commonMcpTools = [];

  final List<String> speedList = ["slow", "normal", "fast"];
  final List<int> pitchList = [-2, -1, 0, 1, 2];
  final List<String> memoryTypeList = ["OFF", "SHORT_TERM"];

  void initPageData() async {
    agentNameController = TextEditingController();
    assistantNameController = TextEditingController();
    characterController = TextEditingController();
    memoryController = TextEditingController();

    //listenlanguageswitch → autorefreshvoice tone
    ever(selectedLanguage, (lang) => _updateTtsVoiceList(lang));

    await loadCommonMcpTools();
    await loadTtsList(); //loadTTS → autogeneratelanguagelist
    await loadModelList();

    if (agent != null) {
      isEdit.value = true;
      fillEditData(agent!);
    } else {
      isEdit.value = false;
      setDefaultCreateData();
    }
  }

  //loadTTSdata + generatedynamiclanguagelist
  Future<void> loadTtsList() async {
    ttsData = await XiaoZhiUtil.shared.getTtsList();

    //✅ key：from ttsVoices  key generatelanguagelist
    if (ttsData?.ttsVoices != null) {
      languageList.value = ttsData!.ttsVoices!.keys.toList();
    }

    //initdefaultlanguage
    if (languageList.isNotEmpty && selectedLanguage.isEmpty) {
      selectedLanguage.value = languageList.first;
    }

    //updatevoice tone
    _updateTtsVoiceList(selectedLanguage.value);
  }

  //switchlanguage → switchvoice tone
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
  }

  Future<void> loadCommonMcpTools() async {
    commonMcpTools = await XiaoZhiUtil.shared.getCommonMcpTool();
    update();
  }

  void fillEditData(Agent agent) {
    agentNameController.text = agent.agent_name ?? "";
    assistantNameController.text = agent.assistant_name ?? "";
    characterController.text = agent.character ?? "";
    memoryController.text = agent.memory ?? "";

    //Editmode:languagemustexistdynamiclistin
    if (languageList.contains(agent.language)) {
      selectedLanguage.value = agent.language!;
    } else if (languageList.isNotEmpty) {
      selectedLanguage.value = languageList.first;
    }

    ttsSpeed.value = agent.tts_speech_speed ?? "normal";
    ttsPitch.value = agent.tts_pitch ?? 0;
    asrSpeed.value = agent.asr_speed ?? "normal";
    memoryType.value = agent.memory_type ?? "SHORT_TERM";

    if (agent.llm_model != null) {
      selectedModel.value = modelList.firstWhereOrNull(
        (m) => m.name == agent.llm_model,
      );
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
    if (agentNameController.text.isEmpty) {
      AppState.shared.showToast("Please enter the AI Agent name.");
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
      agent_name: agentNameController.text.trim(),
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
      mcp_endpoints: selectedMcpEndpoints,
      product_mcp_endpoints: [],
    );

    bool result = false;
    if (isEdit.value) {
      result = await XiaoZhiUtil.shared.updateAgent(agent!.id!, agentParams);
    } else {
      final agentId = await XiaoZhiUtil.shared.createAgent(agentParams);
      result = agentId != null;
    }

    isLoading.value = false;
    if (result) {
      AppState.shared.showToast(isEdit.value
          ? "Agent edited successfully"
          : "Agent created successfully");
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

class _EditAgentState extends State<EditAgent> {
  AgentConfigurationModel agentConfigurationModel =
      Get.find<AgentConfigurationModel>();
  late EditAgentModel model;

  int selectedItem = 0;

  String getLanguagesTitle(String lg) {
    if (ValueConstant.languages[lg] != null) {
      return ValueConstant.languages[lg]!;
    } else {
      return lg;
    }
  }

  @override
  void initState() {
    super.initState();
    model = EditAgentModel(widget.agent);
    model.initPageData();
  }

  @override
  void dispose() {
    model.onClose();
    if (mounted) {
      FocusScope.of(context).unfocus();
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return CupertinoPageScaffold(
      navigationBar: CupertinoNavigationBar.large(
        largeTitle: Obx(
          () => Text(
            model.isEdit.value
                ? (agentConfigurationModel.currentBindAgent.value?.agent_name ??
                      "Edit Agent") //error
                : "Create AI Agent", //translated comment
          ),
        ),
        leading: CupertinoNavigationBarBackButton(),
        backgroundColor: CupertinoColors.systemBackground.resolveFrom(context),
        trailing: Obx(
          () => model.isLoading.value
              ? const CupertinoActivityIndicator(radius: 10)
              : CupertinoButton(
                  padding: EdgeInsets.zero,
                  onPressed: () async {
                    final result = await model.submitAgent();
                    if (result && mounted) {
                      Navigator.pop(this.context);
                    }
                  },
                  child: const Icon(CupertinoIcons.check_mark),
                ),
        ),
      ),
      backgroundColor: CupertinoColors.systemBackground.resolveFrom(context),
      child: Obx(
        () => model.isLoading.value
            ? const Center(child: CupertinoActivityIndicator())
            : SingleChildScrollView(
                padding: const EdgeInsets.symmetric(
                  horizontal: 16,
                  vertical: 20,
                ),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    //1. info
                    _buildSectionTitle("Basic Information"),
                    // _buildInputItem(
                    //   title: "Agent Name",
                    //   controller: model.agentNameController,
                    //   placeholder:
                    //"Please enter the name of the AI Agent.", // System1
                    // ),
                    _buildInputItem(
                      title: "Assistant Name", //translated comment
                      controller: model.assistantNameController,
                      placeholder:
                          "Please enter the assistant's name (e.g. StackChan).",
                    ),
                    //2. modelconfig
                    _buildSectionTitle("Model Configuration"), //standard
                    _buildSelectItem(
                      title: "LLM Model", //（Large Language Model）
                      value: model.selectedModel.value?.name ?? "Please select",
                      onTap: showModelPicker,
                    ),
                    _buildSelectItem(
                      title: "Language",
                      value: getLanguagesTitle(model.selectedLanguage.value),
                      onTap: showLanguagePicker,
                    ),
                    //3. config
                    _buildSectionTitle("Voice Configuration"), //standard
                    _buildSelectItem(
                      title: "Voice Tone", //Timbre，
                      value:
                          model.selectedTtsVoice.value?.voiceName ??
                          "Please select",
                      onTap: showTtsPicker,
                    ),
                    _buildSelectItem(
                      title: "TTS Speech Speed", //isTTS
                      value: model.ttsSpeed.value,
                      onTap: showSpeedPicker,
                    ),
                    _buildSelectItem(
                      title: "TTS Pitch", //isTTS
                      value: model.ttsPitch.value.toString(),
                      onTap: showPitchPicker,
                    ),
                    _buildSelectItem(
                      title: "ASR Speed", //（Automatic Speech Recognition）
                      value: model.asrSpeed.value,
                      onTap: showAsrSpeedPicker,
                    ),
                    //4. characterconfig
                    _buildSectionTitle("Character Configuration"), //standard
                    _buildInputItem(
                      title: "Character Description",
                      controller: model.characterController,
                      placeholder:
                          "Please provide the character description (max 2000 characters).",
                      //(words→characters)
                      maxLines: 4,
                    ),
                    _buildInputItem(
                      title: "Short-term Memory Content", //translated comment
                      controller: model.memoryController,
                      placeholder:
                          "Please enter the short-term memory content.",
                      maxLines: 3,
                    ),
                    _buildSelectItem(
                      title: "Memory Type", //standard
                      value: model.memoryType.value == "SHORT_TERM"
                          ? "Short-term Memory" //standard
                          : "Disabled", //Shut down，UI
                      onTap: showMemoryTypePicker,
                    ),
                    //5. MCPtoolconfig(Partoptimize)
                    // _buildSectionTitle("MCP Tools (Optional)"),
                    // model.commonMcpTools.isEmpty
                    //     ? const Center(child: Text("No available MCP tools"))
                    //     : Wrap(
                    //         spacing: 8,
                    //         runSpacing: 8,
                    //         children: model.commonMcpTools.map((tool) {
                    //           final isSelected = model.selectedMcpEndpoints
                    //               .contains(tool.endpoint_id);
                    //           return CupertinoButton(
                    //             padding: const EdgeInsets.symmetric(
                    //               horizontal: 12,
                    //               vertical: 6,
                    //             ),
                    //             color: isSelected
                    //                 ? CupertinoColors.activeBlue
                    //                 : CupertinoColors.systemGrey5,
                    //             onPressed: () =>
                    //                 model.toggleMcpTool(tool.endpoint_id),
                    //             child: Text(
                    //               tool.name ?? "",
                    //               style: TextStyle(
                    //                 color: isSelected
                    //                     ? CupertinoColors.white
                    //                     : CupertinoColors.black,
                    //                 fontSize: 14,
                    //               ),
                    //             ),
                    //           );
                    //         }).toList(),
                    //       ),
                    const SizedBox(height: 40),
                  ],
                ),
              ),
      ),
    );
  }

  void showModelPicker() {
    final initialIndex = model.modelList.indexOf(model.selectedModel.value);
    showPicker(
      title: "Select LLM Model",
      items: model.modelList.map((e) => e.name ?? "").toList(),
      initialIndex: initialIndex.clamp(0, model.modelList.length - 1),
      onSelected: (index) => model.selectedModel.value = model.modelList[index],
    );
  }

  void showLanguagePicker() {
    final initialIndex = model.languageList.indexOf(
      model.selectedLanguage.value,
    );
    showPicker(
      title: "Select Language",
      items: model.languageList
          .map((value) => getLanguagesTitle(value))
          .toList(),
      initialIndex: initialIndex.clamp(0, model.languageList.length - 1),
      onSelected: (index) {
        //fix:AssignValue languageKEY(zh/en),And / WhileNotisshowname!
        model.selectedLanguage.value = model.languageList[index];
      },
    );
  }

  void showTtsPicker() {
    final initialIndex = model.ttsList.indexOf(model.selectedTtsVoice.value);
    showPicker(
      title: "Select Voice Tone",
      items: model.ttsList.map((e) => e.voiceName ?? "").toList(),
      initialIndex: initialIndex.clamp(0, model.ttsList.length - 1),
      onSelected: (index) =>
          model.selectedTtsVoice.value = model.ttsList[index],
    );
  }

  void showSpeedPicker() {
    final initialIndex = model.speedList.indexOf(model.ttsSpeed.value);
    showPicker(
      title: "Select TTS Speech Speed",
      items: model.speedList,
      initialIndex: initialIndex.clamp(0, model.speedList.length - 1),
      onSelected: (index) => model.ttsSpeed.value = model.speedList[index],
    );
  }

  void showPitchPicker() {
    final initialIndex = model.pitchList.indexOf(model.ttsPitch.value);
    showPicker(
      title: "Select TTS Pitch",
      items: model.pitchList.map((e) => e.toString()).toList(),
      initialIndex: initialIndex.clamp(0, model.pitchList.length - 1),
      onSelected: (index) => model.ttsPitch.value = model.pitchList[index],
    );
  }

  void showAsrSpeedPicker() {
    final initialIndex = model.speedList.indexOf(model.asrSpeed.value);
    showPicker(
      title: "Select ASR Speed",
      items: model.speedList,
      initialIndex: initialIndex.clamp(0, model.speedList.length - 1),
      onSelected: (index) => model.asrSpeed.value = model.speedList[index],
    );
  }

  void showMemoryTypePicker() {
    final initialIndex = model.memoryTypeList.indexOf(model.memoryType.value);
    showPicker(
      title: "Select Memory Type",
      items: model.memoryTypeList
          .map((e) => e == "SHORT_TERM" ? "Short-term Memory" : "Disabled")
          .toList(),
      initialIndex: initialIndex.clamp(0, model.memoryTypeList.length - 1),
      onSelected: (index) =>
          model.memoryType.value = model.memoryTypeList[index],
    );
  }

  void showPicker({
    required String title,
    required List<String> items,
    required Function(int) onSelected,
    int initialIndex = 0, //new：selectedindex
  }) {
    //initselectedItemascurrentValue(fixdefaultselectederror)
    selectedItem = initialIndex;
    showCupertinoModalPopup(
      context: context,
      builder: (context) {
        return Container(
          height: 260,
          color: CupertinoColors.systemBackground.resolveFrom(context),
          child: Column(
            children: [
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  CupertinoButton(
                    child: const Text("Cancel"),
                    onPressed: () => Navigator.pop(context),
                  ),
                  CupertinoButton(
                    child: const Text("Confirm"),
                    onPressed: () {
                      Navigator.pop(context);
                      onSelected(selectedItem);
                    },
                  ),
                ],
              ),
              Expanded(
                child: CupertinoPicker(
                  itemExtent: 32,
                  magnification: 1.22,
                  squeeze: 1.2,
                  useMagnifier: true,
                  scrollController: FixedExtentScrollController(
                    initialItem: initialIndex,
                  ),
                  onSelectedItemChanged: (index) => selectedItem = index,
                  children: items.map((e) => Text(e)).toList(),
                ),
              ),
            ],
          ),
        );
      },
    );
  }

  //title
  Widget _buildSectionTitle(String title) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 12),
      child: Text(
        title,
        style: const TextStyle(fontSize: 18, fontWeight: FontWeight.w600),
      ),
    );
  }

  //inputItem
  Widget _buildInputItem({
    required String title,
    required TextEditingController controller,
    required String placeholder,
    int maxLines = 1,
  }) {
    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            title,
            style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w500),
          ),
          const SizedBox(height: 6),
          CupertinoTextField(
            controller: controller,
            placeholder: placeholder,
            maxLines: maxLines,
            minLines: maxLines,
            textAlign: TextAlign.left,
            decoration: BoxDecoration(
              color: CupertinoColors.systemGrey6,
              borderRadius: BorderRadius.circular(8),
            ),
            padding: const EdgeInsets.all(12),
          ),
        ],
      ),
    );
  }

  //selectItem
  Widget _buildSelectItem({
    required String title,
    required String value,
    required VoidCallback onTap,
  }) {
    return CupertinoButton(
      padding: EdgeInsets.zero,
      onPressed: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(vertical: 12),
        decoration: const BoxDecoration(
          border: Border(
            bottom: BorderSide(color: CupertinoColors.systemGrey5, width: 0.5),
          ),
        ),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(
              title,
              style: TextStyle(
                fontSize: 15,
                color: CupertinoColors.label.resolveFrom(context),
              ),
            ),
            Row(
              children: [
                Text(
                  value,
                  style: TextStyle(
                    fontSize: 15,
                    color: CupertinoColors.secondaryLabel.resolveFrom(context),
                  ),
                ),
                const SizedBox(width: 6),
                Icon(
                  CupertinoIcons.chevron_right,
                  size: 14,
                  color: CupertinoColors.secondaryLabel.resolveFrom(context),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

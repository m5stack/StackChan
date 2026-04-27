/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';

import 'package:flutter/cupertino.dart';
import 'package:get/get.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/view/popup/edit_agent.dart';

import '../../model/XiaoZhi/agent.dart';
import '../../model/XiaoZhi/agent_template.dart';
import '../../model/XiaoZhi/device.dart';
import '../../util/XiaoZhi_util.dart';

//System1UIConstant - 提升Can维护性
const double kCardRadius = 16.0;
const double kDefaultPadding = 16.0;
const double kDefaultSpacing = 16.0;
const Color kPrimaryTintColor = CupertinoColors.systemBlue;
const double kElevation = 2.0;

class AgentConfiguration extends StatefulWidget {
  const AgentConfiguration({super.key});

  @override
  State<StatefulWidget> createState() => _AgentConfigurationState();
}

class AgentConfigurationModel extends GetxController {
  Rxn<Device> device = Rxn(null);
  RxList<AgentTemplate> agentTemplatesList = RxList([]);
  Rxn<Agent> currentBindAgent = Rxn(null);

  //分页search相关
  int agentListPage = 1;
  RxBool isLoading = false.obs;
  RxBool isDialogLoading = false.obs;
  RxBool isListLoading = false.obs;
  bool hasMoreList = true;

  Future<void> loadAllData() async {
    isLoading.value = true;
    try {
      await Future.wait([loadDevice(), loadAgentTemplates()]);
    } catch (e) {
      debugPrint("Failed to initialize data: $e");
    } finally {
      isLoading.value = false;
    }
  }

  //loaddeviceinfo
  Future<void> loadDevice() async {
    final devices = await XiaoZhiUtil.shared.getDevice(
      AppState.shared.deviceMac,
    );
    if (devices.isNotEmpty) {
      device.value = devices.first;
      if (device.value?.agent_id != null) {
        await getBindAgent(device.value!.agent_id!);
      } else {
        currentBindAgent.value = null;
      }
    } else {
      device.value = null;
      currentBindAgent.value = null;
    }
  }

  //Getdeviceboundagentdetails
  Future<void> getBindAgent(int agentId) async {
    final agent = await XiaoZhiUtil.shared.getAgentDetail(agentId);
    currentBindAgent.value = agent;
  }

  //switchbindagent
  Future<bool> switchBindAgent(
    Agent targetAgent,
    String verificationCode,
  ) async {
    if (device.value == null) {
      AppState.shared.showToast("No device detected, cannot bind AI Agent");
      return false;
    }
    if (targetAgent.id == null) {
      AppState.shared.showToast("Invalid AI Agent ID, cannot bind");
      return false;
    }
    if (verificationCode.isEmpty) {
      AppState.shared.showToast("Please enter device verification code");
      return false;
    }
    if (currentBindAgent.value?.id == targetAgent.id) {
      AppState.shared.showToast("Device is already bound to this AI Agent");
      return true;
    }

    isLoading.value = true;
    try {
      //First / Previouslyunbindcurrentdevice
      if (device.value?.device_id != null) {
        await XiaoZhiUtil.shared.unbindDevice(device.value!.device_id!);
      }
      //bindtonewagent
      final bindSuccess = await XiaoZhiUtil.shared.bindDeviceToAgent(
        targetAgent.id!,
        verificationCode,
      );
      if (!bindSuccess) {
        throw Exception("Failed to bind device to AI Agent");
      }
      //refreshdata
      await loadDevice();
      AppState.shared.showToast("Successfully switched AI Agent");
      return true;
    } catch (e) {
      debugPrint("Failed to switch bound AI Agent: $e");
      AppState.shared.showToast(e.toString().replaceAll("Exception: ", ""));
      return false;
    } finally {
      isLoading.value = false;
    }
  }

  //loadagent模板
  Future<void> loadAgentTemplates() async {
    final templates = await XiaoZhiUtil.shared.agentTemplatesList(
      agentListPage,
      20,
    );
    agentTemplatesList.assignAll(templates);
  }

  //Replace字符Descriptionin占位符
  String replacePlaceholdersInCharacter(Agent agent) {
    if (agent.character == null || agent.character!.isEmpty) {
      return "";
    }
    String characterText = agent.character!;
    if (agent.assistant_name != null && agent.assistant_name!.isNotEmpty) {
      characterText = characterText.replaceAll(
        "{{assistant_name}}",
        agent.assistant_name!,
      );
    }
    if (agent.user_name != null && agent.user_name!.isNotEmpty) {
      characterText = characterText.replaceAll(
        "{{user_name}}",
        agent.user_name!,
      );
    }
    return characterText;
  }
}

class _AgentConfigurationState extends State<AgentConfiguration> {
  late AgentConfigurationModel model = Get.put(AgentConfigurationModel());

  final ScrollController scrollController = ScrollController();

  @override
  void dispose() {
    scrollController.dispose();
    Get.delete<AgentConfigurationModel>();
    super.dispose();
  }

  @override
  void initState() {
    super.initState();
    model.loadAllData();
    scrollController.addListener(() {
      if (scrollController.position.pixels >=
          scrollController.position.maxScrollExtent - 200) {
        if (!model.isListLoading.value && model.hasMoreList) {
          model.loadAgentTemplates();
        }
      }
    });
  }

  ///showCreate agentpopup
  void goToEditAgentPage({bool isCreate = false, Agent? editAgent}) {
    Navigator.of(context)
        .push(
          CupertinoPageRoute(
            builder: (context) {
              return EditAgent(agent: editAgent);
            },
          ),
        )
        .then((_) {
          model.loadAllData();
        });
  }

  @override
  Widget build(BuildContext context) {
    //GetCupertinodynamicTheme色
    final CupertinoThemeData cupertinoTheme = CupertinoTheme.of(context);
    final Color surfaceColor = CupertinoColors.systemGroupedBackground
        .resolveFrom(context);
    final Color secondaryTextColor = CupertinoColors.secondaryLabel.resolveFrom(
      context,
    );
    final Color primaryColor = cupertinoTheme.primaryColor;

    return CupertinoPageScaffold(
      backgroundColor: surfaceColor,
      navigationBar: CupertinoNavigationBar.large(
        largeTitle: Obx(
          () => Text(
            model.currentBindAgent.value?.agent_name ?? "AI Agent",
            style: const TextStyle(fontWeight: FontWeight.w600),
          ),
        ),
        leading: CupertinoButton(
          padding: EdgeInsets.zero,
          child: const Icon(CupertinoIcons.xmark, size: 24),
          onPressed: () => CupertinoSheetRoute.popSheet(context),
        ),
        backgroundColor: surfaceColor,
        trailing: CupertinoButton(
          padding: EdgeInsets.zero,
          onPressed: () => goToEditAgentPage(
            isCreate: false,
            editAgent: model.currentBindAgent.value,
          ),
          child: Icon(
            CupertinoIcons.pencil_circle,
            size: 26,
            color: primaryColor,
          ),
        ),
        border: const Border(bottom: BorderSide.none), //remove底部边框更现代
      ),
      child: Obx(() {
        //全局Initialloadstate
        if (model.isLoading.value) {
          return const Center(
            child: CupertinoActivityIndicator(radius: 16), //increaseloadindicator
          );
        }
        //Nodevicestate
        if (model.device.value == null) {
          return _buildNoDeviceWidget(secondaryTextColor, primaryColor);
        }
        //MainLayout
        return ListView(
          controller: scrollController,
          padding: const EdgeInsets.symmetric(
            horizontal: kDefaultPadding,
            vertical: 24,
          ),
          children: [
            //currentbindagent卡片
            _buildCurrentBindCard(
              cupertinoTheme,
              primaryColor,
              secondaryTextColor,
            ),
            const SizedBox(height: 32),
            //increase间距提升呼吸感
            //listtitle
            // const Text(
            //   "AI Agent Templates",
            //   style: TextStyle(
            //     fontSize: 20,
            //     fontWeight: FontWeight.w600,
            //letterSpacing: -0.5, // 轻微Zoom out字间距更精致
            //   ),
            // ),
            // const SizedBox(height: 16),
            //// Agent templatelist
            // _buildAgentTemplatesList(cupertinoTheme, primaryColor),
            //// loadmoreindicator
            // if (model.isListLoading.value)
            //   const Padding(
            //     padding: EdgeInsets.symmetric(vertical: 24),
            //     child: Center(child: CupertinoActivityIndicator(radius: 14)),
            //   ),
            const SizedBox(height: 40),
          ],
        );
      }),
    );
  }

  //currentbindagent卡片 - optimize视觉质感
  Widget _buildCurrentBindCard(
    CupertinoThemeData theme,
    Color primaryColor,
    Color secondaryTextColor,
  ) {
    final cardRadius = 40.0;

    final currentAgent = model.currentBindAgent.value;
    if (currentAgent == null) {
      return ClipRSuperellipse(
        borderRadius: .circular(cardRadius),
        child: Container(
          padding: .all(cardRadius / 2),
          color: CupertinoColors.systemBackground.resolveFrom(context),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(
                CupertinoIcons.person_badge_plus, //use填充icon更现代
                size: 56,
                color: secondaryTextColor.withValues(alpha: 0.7),
              ),
              const SizedBox(height: 16),
              const Text(
                "No Agent Bound",
                style: TextStyle(
                  fontSize: 20,
                  fontWeight: FontWeight.w600,
                  letterSpacing: -0.3,
                ),
              ),
              const SizedBox(height: 12),
              const Text(
                "Select an Agent template below to bind, or create a custom AI Agent by tapping the edit button in the upper right corner.",
                style: TextStyle(
                  fontSize: 15,
                  height: 1.5, //行高提升可读性
                ),
                textAlign: TextAlign.center,
              ),
            ],
          ),
        ),
      );
    }

    String processedCharacter = model.replacePlaceholdersInCharacter(
      currentAgent,
    );

    return ClipRSuperellipse(
      borderRadius: .circular(cardRadius),
      child: Container(
        padding: .all(cardRadius / 2),
        color: CupertinoColors.systemBackground.resolveFrom(context),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: .min,
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                const Text(
                  "Current AI Agent",
                  style: TextStyle(
                    fontSize: 17,
                    fontWeight: FontWeight.w600,
                    letterSpacing: -0.3,
                  ),
                ),
                Text(
                  "ID: ${currentAgent.id}",
                  style: TextStyle(
                    fontSize: 12,
                    color: secondaryTextColor.withValues(alpha: 0.8),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 16),
            _buildInfoItem(
              "AI Model",
              currentAgent.llm_model ?? "Not configured",
              secondaryTextColor,
            ),
            _buildInfoItem(
              "Voice Tone",
              currentAgent.tts_voice ?? "Not configured",
              secondaryTextColor,
            ),
            _buildInfoItem(
              "Speech Speed",
              currentAgent.tts_speech_speed ?? "Normal",
              secondaryTextColor,
            ),
            _buildInfoItem(
              "Pitch",
              currentAgent.tts_pitch?.toString() ?? "0",
              secondaryTextColor,
            ),
            if (processedCharacter.isNotEmpty)
              Padding(
                padding: const EdgeInsets.only(top: 16, bottom: 8),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      "Character Profile",
                      style: TextStyle(
                        fontSize: 15,
                        fontWeight: FontWeight.w600,
                        letterSpacing: -0.2,
                      ),
                    ),
                    const SizedBox(height: 8),
                    ClipRSuperellipse(
                      borderRadius: .circular(cardRadius / 2),
                      child: Container(
                        padding: .all(cardRadius / 4),
                        color: CupertinoColors.systemGroupedBackground
                            .resolveFrom(context),
                        child: Text(
                          processedCharacter,
                          style: TextStyle(
                            fontSize: 14,
                            color: secondaryTextColor,
                            height: 1.6,
                          ),
                        ),
                      ),
                    ),
                  ],
                ),
              ),
          ],
        ),
      ),
    );
  }

  //Nodevicehintcomponent - optimize视觉表现
  Widget _buildNoDeviceWidget(Color secondaryTextColor, Color primaryColor) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              CupertinoIcons.nosign,
              size: 72,
              color: secondaryTextColor.withValues(alpha: 0.6),
            ),
            const SizedBox(height: 24),
            const Text(
              "No Device Detected",
              style: TextStyle(
                fontSize: 22,
                fontWeight: FontWeight.w600,
                letterSpacing: -0.5,
              ),
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 12),
            Text(
              "We couldn't find any connected devices. Please check your device connection and try again.",
              style: TextStyle(
                fontSize: 15,
                color: secondaryTextColor,
                height: 1.5,
              ),
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 32),
            CupertinoButton.filled(
              borderRadius: BorderRadius.circular(12),
              padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 14),
              onPressed: model.loadDevice,
              child: const Text(
                "Retry Detection",
                style: TextStyle(fontSize: 15, fontWeight: FontWeight.w500),
              ),
            ),
          ],
        ),
      ),
    );
  }

  //通用infoItemcomponent - optimize排版
  Widget _buildInfoItem(String title, String value, Color secondaryTextColor) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(
            title,
            style: const TextStyle(
              fontSize: 15,
              fontWeight: FontWeight.w500,
              letterSpacing: -0.2,
            ),
          ),
          Text(
            value,
            style: TextStyle(
              fontSize: 15,
              color: secondaryTextColor,
              letterSpacing: -0.1,
            ),
          ),
        ],
      ),
    );
  }
}

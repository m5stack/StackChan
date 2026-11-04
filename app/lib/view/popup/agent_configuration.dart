/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/popup.dart';
import 'package:stack_chan/view/util/theme_setting.dart';
import 'package:get/get.dart';
import 'package:liquid_glass_widgets/liquid_glass_widgets.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/view/popup/edit_agent.dart';

import '../../model/XiaoZhi/agent.dart';
import '../../model/XiaoZhi/agent_template.dart';
import '../../model/XiaoZhi/device.dart';
import '../../util/xiao_zhi_util.dart';

// Shared UI constants - keeps layout tweaks in one place.
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

  // Pagination and list-loading state.
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

  // Load device info.
  Future<void> loadDevice() async {
    final devices = await XiaoZhiUtil.shared.getDevice(
      AppState.shared.deviceMac,
    );
    if (devices.isNotEmpty) {
      device.value = devices.first;
      if (device.value?.agentId != null) {
        await getBindAgent(device.value!.agentId!);
      } else {
        currentBindAgent.value = null;
      }
    } else {
      device.value = null;
      currentBindAgent.value = null;
    }
  }

  // Fetch the agent currently bound to the device.
  Future<void> getBindAgent(int agentId) async {
    final agent = await XiaoZhiUtil.shared.getAgentDetail(agentId);
    currentBindAgent.value = agent;
  }

  // Switch the agent bound to the device.
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
      // Unbind the current device first.
      if (device.value?.deviceId != null) {
        await XiaoZhiUtil.shared.unbindDevice(device.value!.deviceId!);
      }
      // Bind to the new agent.
      final bindSuccess = await XiaoZhiUtil.shared.bindDeviceToAgent(
        targetAgent.id!,
        verificationCode,
      );
      if (!bindSuccess) {
        throw Exception("Failed to bind device to AI Agent");
      }
      // Refresh local data.
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

  // Load agent templates.
  Future<void> loadAgentTemplates() async {
    final templates = await XiaoZhiUtil.shared.agentTemplatesList(
      agentListPage,
      20,
    );
    agentTemplatesList.assignAll(templates);
  }

  // Strip assistant-name placeholders and fill in the user's name.
  String replacePlaceholdersInCharacter(Agent agent) {
    if (agent.character == null || agent.character!.isEmpty) {
      return "";
    }
    String characterText = agent.character!;
    characterText = characterText
        .replaceAll("{{assistantName}}", "")
        .replaceAll("{{assistant_name}}", "");
    if (agent.userName != null && agent.userName!.isNotEmpty) {
      characterText = characterText.replaceAll("{{userName}}", agent.userName!);
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

  /// Opens the agent editor for [editAgent], or for a new agent when it is null.
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
    // Resolve theme colors once for this build.
    final CupertinoThemeData cupertinoTheme = CupertinoTheme.of(context);
    final Color secondaryTextColor = CupertinoColors.secondaryLabel.resolveFrom(
      context,
    );
    final Color primaryColor = cupertinoTheme.primaryColor;
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
        largeTitle: Obx(
          () => Text(
            model.currentBindAgent.value?.agentName ?? "AI Agent",
            style: const TextStyle(fontWeight: FontWeight.w600),
          ),
        ),
        leading: CupertinoButton(
          sizeStyle: .medium,
          child: const Icon(CupertinoIcons.xmark, size: 24),
          onPressed: () => closeCustomCupertinoSheet(context),
        ),
        trailing: CupertinoButton(
          sizeStyle: .medium,
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
      ),
      child: Obx(() {
        // Global loading state.
        if (model.isLoading.value) {
          return const Center(
            child: CupertinoActivityIndicator(
              radius: 16,
            ), // Larger loading indicator
          );
        }
        // No device available.
        if (model.device.value == null) {
          return _buildNoDeviceWidget(secondaryTextColor, primaryColor);
        }
        // Main layout.
        return ListView(
          controller: scrollController,
          padding: .only(
            left: padding.left + kDefaultPadding,
            right: padding.right + kDefaultPadding,
            top: 24,
            bottom: 24,
          ),
          children: [
            // Card for the currently bound agent.
            _buildCurrentBindCard(
              cupertinoTheme,
              primaryColor,
              secondaryTextColor,
            ),
            const SizedBox(height: 32),
            // Extra spacing for breathing room.
            //listTitle
            // const Text(
            //   "AI Agent Templates",
            //   style: TextStyle(
            //     fontSize: 20,
            //     fontWeight: FontWeight.w600,
            //letterSpacing: -0.5, // Slightly tighter spacing looks finer
            //   ),
            // ),
            // const SizedBox(height: 16),
            //// Agent template list
            // _buildAgentTemplatesList(cupertinoTheme, primaryColor),
            //// Load-more indicator
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

  // Card showing the currently bound agent.
  Widget _buildCurrentBindCard(
    CupertinoThemeData theme,
    Color primaryColor,
    Color secondaryTextColor,
  ) {
    const cardRadius = 40.0;

    final currentAgent = model.currentBindAgent.value;
    if (currentAgent == null) {
      return ClipRSuperellipse(
        borderRadius: .circular(cardRadius),
        child: Container(
          padding: const .all(cardRadius / 2),
          color: CupertinoColors.systemBackground.resolveFrom(context),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(
                CupertinoIcons.person_badge_plus, // Filled icon looks more modern
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
                  height: 1.5, // Taller line height improves readability
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
        padding: const .all(cardRadius / 2),
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
              currentAgent.llmModel ?? "Not configured",
              secondaryTextColor,
            ),
            _buildInfoItem(
              "Voice Tone",
              currentAgent.ttsVoice ?? "Not configured",
              secondaryTextColor,
            ),
            _buildInfoItem(
              "Speech Speed",
              currentAgent.ttsSpeechSpeed ?? "Normal",
              secondaryTextColor,
            ),
            _buildInfoItem(
              "Pitch",
              currentAgent.ttsPitch?.toString() ?? "0",
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
                        padding: const .all(cardRadius / 4),
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

  // Empty state shown when no device is available.
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
            SizedBox(
              width: 220,
              child: GlassButton.custom(
                height: 50,
                settings: LiquidGlassSettings(
                  glassColor: CupertinoTheme.of(context).primaryColor,
                ),
                shape: const LiquidRoundedSuperellipse(borderRadius: 23),
                onTap: model.loadDevice,
                child: const Center(
                  child: Text(
                    "Retry Detection",
                    style: TextStyle(
                      color: CupertinoColors.white,
                      fontSize: 15,
                      fontWeight: FontWeight.w500,
                    ),
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  // Label/value row used for the agent details.
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

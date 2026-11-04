/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/theme_setting.dart';
import 'package:flutter_slidable/flutter_slidable.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:get/get.dart';
import 'package:intl/intl.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/util/xiao_zhi_util.dart';
import 'package:stack_chan/view/popup/conversation_message_page.dart';

import '../../model/XiaoZhi/conversation.dart';
import '../util/popup.dart';

class ConversationPage extends StatefulWidget {
  const ConversationPage({super.key});

  @override
  State<StatefulWidget> createState() => _ConversationPageState();
}

class _ConversationPageState extends State<ConversationPage> {
  RxList<Conversation> conversationList = RxList([]);

  final DateFormat dateFormat = DateFormat("yyyy-MM-dd HH:mm");

  int page = 1;
  int pageSize = 100;

  @override
  void initState() {
    super.initState();
    page = 1;
    getList();
  }

  void getList() async {
    final devices = await XiaoZhiUtil.shared.getDevice(
      AppState.shared.deviceMac,
    );
    if (devices.isNotEmpty && devices.first.deviceId != null) {
      final deviceId = devices.first.deviceId;
      final agentId = devices.first.agentId;
      await AppState.shared.getDeviceInfo();
      String startDate = "";

      if (AppState.shared.deviceInfo.value?.bindTime != null) {
        if (AppState.shared.deviceInfo.value!.bindTime!.isNotEmpty) {
          String bindTime = AppState.shared.deviceInfo.value!.bindTime!;
          startDate = bindTime.split(" ").first;
        }
      }
      if (startDate == "") {
        DateTime tenDaysAgo = DateTime.now().subtract(const Duration(days: 10));
        startDate =
            "${tenDaysAgo.year.toString()}-${tenDaysAgo.month.toString().padLeft(2, '0')}-${tenDaysAgo.day.toString().padLeft(2, '0')}";
      }
      if (deviceId != null) {
        conversationList.value = await XiaoZhiUtil.shared.getConversationList(
          startDate: startDate,
          deviceId: deviceId,
          page: page,
          pageSize: pageSize,
          agentId: agentId,
        );
      }
    }
  }

  String formatTime(String? timeStr) {
    if (timeStr == null || timeStr.isEmpty) return 'Unknown time';
    try {
      DateTime dateTime = DateTime.parse(timeStr);
      return dateFormat.format(dateTime);
    } catch (e) {
      return timeStr;
    }
  }

  Future<void> deleteConversation(int? id, int? agentId) async {
    if (id != null && agentId != null) {
      final result = await XiaoZhiUtil.shared.deleteConversation(agentId, id);
      if (result) {
        getList();
      }
    }
  }

  Widget buildConversationItem(Conversation conversation, int index) {
    String title = conversation.chatSummary?.title ?? "Unlabeled conversation";
    String summary = conversation.chatSummary?.summary ?? "No abstract.";
    String createTime = formatTime(conversation.createdAt);
    String model = conversation.model ?? "Unknown model";
    int msgCount = conversation.msgCount ?? 0;
    int tokenCount = conversation.tokenCount ?? 0;
    int? chatId = conversation.id;

    return Slidable(
      key: ValueKey(index),
      endActionPane: ActionPane(
        extentRatio: 0.25,
        motion: const ScrollMotion(),
        children: [
          // Delete button
          SlidableAction(
            onPressed: (_) => deleteConversation(chatId, conversation.agentId),
            backgroundColor: CupertinoColors.systemRed.withValues(alpha: 0.9),
            foregroundColor: CupertinoColors.white,
            icon: CupertinoIcons.trash,
            label: 'Delete',
          ),
        ],
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
        child: CupertinoListTile(
          title: Text(
            title,
            style: TextStyle(
              fontSize: 16,
              fontWeight: .w500,
              color: CupertinoColors.label.resolveFrom(context),
            ),
            maxLines: 1,
            overflow: .ellipsis,
          ),
          subtitle: Column(
            crossAxisAlignment: .start,
            children: [
              Text(
                summary,
                style: TextStyle(
                  fontSize: 14,
                  color: CupertinoColors.secondaryLabel.resolveFrom(context),
                ),
                maxLines: 2,
                overflow: .ellipsis,
              ),
              const SizedBox(height: 4),
              Text(
                createTime,
                style: TextStyle(
                  fontSize: 12,
                  color: CupertinoColors.tertiaryLabel.resolveFrom(context),
                ),
              ),
            ],
          ),
          trailing: Column(
            mainAxisAlignment: .center,
            crossAxisAlignment: .end,
            children: [
              Text(
                "$msgCount Message",
                style: TextStyle(
                  fontSize: 12,
                  color: CupertinoColors.secondaryLabel.resolveFrom(context),
                ),
              ),
              const SizedBox(height: 2),
              Text(
                model,
                style: TextStyle(
                  fontSize: 11,
                  color: CupertinoColors.tertiaryLabel.resolveFrom(context),
                ),
              ),
              const SizedBox(height: 2),
              Text(
                "Token: $tokenCount",
                style: TextStyle(
                  fontSize: 10,
                  color: CupertinoColors.quaternaryLabel.resolveFrom(context),
                ),
              ),
            ],
          ),
          onTap: () {
            if (chatId != null) {
              showCustomCupertinoSheet(
                context: context,
                scrollableBuilder: (context, scrollController) {
                  return ConversationMessagePage(chatId: chatId);
                },
              );
            }
          },
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final padding = MediaQuery.paddingOf(context);
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      child: Obx(
        () => CustomScrollView(
          slivers: [
            CupertinoSliverNavigationBar(
              backgroundColor:
                  ThemeSetting.cupertinoNavigationBarBackgroundColor(context),
              border: ThemeSetting.cupertinoNavigationBarBorder(context),
              padding: ThemeSetting.navigationBarPadding,
              largeTitle: const Text("Chat History"),
            ),
            CupertinoSliverRefreshControl(
              onRefresh: () async {
                page = 1;
                getList();
              },
            ),
            if (conversationList.isEmpty)
              SliverToBoxAdapter(
                child: SizedBox(
                  height: MediaQuery.of(context).size.height - 200,
                  child: Center(
                    child: Column(
                      mainAxisAlignment: .center,
                      children: [
                        SvgPicture.asset(
                          "assets/questionmark.bubble.svg",
                          colorFilter: ColorFilter.mode(
                            CupertinoColors.secondaryLabel.resolveFrom(context),
                            BlendMode.srcIn,
                          ),
                          width: 64,
                          height: 64,
                        ),
                        const SizedBox(height: 16),
                        Text(
                          "No conversation records available.",
                          style: TextStyle(
                            fontSize: 18,
                            color: CupertinoColors.secondaryLabel.resolveFrom(
                              context,
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              )
            else
              SliverPadding(
                padding: .only(left: padding.left, right: padding.right),
                sliver: SliverList.builder(
                  itemCount: conversationList.length,
                  itemBuilder: (context, index) {
                    return buildConversationItem(
                      conversationList[index],
                      index,
                    );
                  },
                ),
              ),
          ],
        ),
      ),
    );
  }
}

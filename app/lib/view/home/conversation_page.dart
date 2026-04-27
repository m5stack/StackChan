/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';
import 'package:flutter_slidable/flutter_slidable.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:get/get.dart';
import 'package:intl/intl.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/util/XiaoZhi_util.dart';
import 'package:stack_chan/view/popup/conversation_message_page.dart';

import '../../model/XiaoZhi/conversation.dart';

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
    if (devices.isNotEmpty && devices.first.device_id != null) {
      final deviceId = devices.first.device_id;
      final agentId = devices.first.agent_id;
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
          startDate,
          deviceId,
          page,
          pageSize,
          agentId,
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
    String title = conversation.chat_summary?.title ?? "Unlabeled conversation";
    String summary = conversation.chat_summary?.summary ?? "No abstract.";
    String createTime = formatTime(conversation.created_at);
    String model = conversation.model ?? "Unknown model";
    int msgCount = conversation.msg_count ?? 0;
    int tokenCount = conversation.token_count ?? 0;
    int? chatId = conversation.id;

    return Slidable(
      key: ValueKey(index),
      endActionPane: ActionPane(
        extentRatio: 0.25,
        motion: ScrollMotion(),
        children: [
          //deletebutton
          SlidableAction(
            onPressed: (_) => deleteConversation(chatId, conversation.agent_id),
            backgroundColor: CupertinoColors.systemRed.withValues(alpha: 0.9),
            foregroundColor: CupertinoColors.white,
            icon: CupertinoIcons.trash,
            label: 'Delete',
          ),
        ],
      ),
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
            showCupertinoSheet(
              context: context,
              builder: (context) {
                return ConversationMessagePage(chatId: chatId);
              },
            );
          }
        },
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return CupertinoPageScaffold(
      child: Obx(
        () => CustomScrollView(
          slivers: [
            CupertinoSliverNavigationBar(largeTitle: Text("Chat History")),
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
                        SizedBox(height: 16),
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
              SliverList.separated(
                itemCount: conversationList.length,
                itemBuilder: (context, index) {
                  return buildConversationItem(conversationList[index], index);
                },
                separatorBuilder: (context, index) => Container(
                  height: 0.5,
                  color: CupertinoColors.separator.resolveFrom(context),
                ),
              ),
          ],
        ),
      ),
    );
  }
}

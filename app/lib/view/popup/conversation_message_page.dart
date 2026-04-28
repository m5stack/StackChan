/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';
import 'package:get/get.dart';
import 'package:intl/intl.dart';
import 'package:stack_chan/model/XiaoZhi/conversation_message_data.dart';
import 'package:stack_chan/util/XiaoZhi_util.dart';

class ConversationMessagePage extends StatefulWidget {
  const ConversationMessagePage({super.key, required this.chatId});

  final int chatId;

  @override
  State<StatefulWidget> createState() => _ConversationMessagePageState();
}

class _ConversationMessagePageState extends State<ConversationMessagePage> {
  RxInt page = RxInt(1);
  int pageSize = 30;

  RxBool isLoading = RxBool(false); //loadstate
  RxBool hasMore = RxBool(true); //whetherhasmoredata

  final DateFormat timeFormat = DateFormat("yyyy-MM-dd HH:mm"); //time

  RxList<ConversationMessageData> messageList = RxList([]);

  final ScrollController scrollController = ScrollController();

  @override
  void initState() {
    super.initState();
    scrollController.addListener(onScroll);
    init();
  }

  void init() async {
    await getMessages(isLoadMore: true);
    WidgetsBinding.instance.addPostFrameCallback((_) {
      scrollToBottom();
    });
  }

  @override
  void dispose() {
    scrollController.dispose();
    super.dispose();
  }

  void scrollToBottom() {
    if (scrollController.hasClients) {
      scrollController.jumpTo(scrollController.position.maxScrollExtent);
    }
  }

  void onScroll() {
    if (scrollController.offset <=
            scrollController.position.minScrollExtent + 50 &&
        !isLoading.value &&
        hasMore.value) {
      getMessages(isLoadMore: true);
    }
  }

  Future<void> getMessages({bool isLoadMore = false}) async {
    if (isLoadMore && !hasMore.value) return;
    isLoading.value = true;
    final Map<String, dynamic> map = {
      "page": page.value,
      "pageSize": pageSize,
      "chatId": widget.chatId,
    };
    final list = await XiaoZhiUtil.shared.getChatsMessages(map);
    final newData = list.reversed.toList();
    if (isLoadMore) {
      messageList.insertAll(0, newData);
      if (newData.length < pageSize) {
        hasMore.value = false;
      }
    } else {
      messageList.value = newData;
      hasMore.value = true;
    }
  }

  Future<void> onRefresh() async {
    page.value++;
    await getMessages(isLoadMore: true);
  }

  String formatMessageTime(String? timeStr) {
    if (timeStr == null || timeStr.isEmpty) return 'Unknown time';
    try {
      DateTime dateTime = DateTime.parse(timeStr);
      return timeFormat.format(dateTime);
    } catch (e) {
      return timeStr;
    }
  }

  Widget buildMessageItem(ConversationMessageData message) {
    bool isUserMessage = message.role == "user";

    Widget messageContent = Container(
      padding: .symmetric(horizontal: 12, vertical: 8),
      margin: .symmetric(horizontal: 16, vertical: 4),
      decoration: BoxDecoration(
        color: isUserMessage
            ? CupertinoTheme.of(context).primaryColor
            : CupertinoColors.secondarySystemBackground.resolveFrom(context),
        borderRadius: .circular(15),
      ),
      child: Column(
        crossAxisAlignment: isUserMessage ? .start : .end,
        mainAxisSize: .min,
        children: [
          Text(
            message.content ?? "Empty message",
            style: TextStyle(
              fontSize: 15,
              color: isUserMessage
                  ? CupertinoColors.white
                  : CupertinoColors.label.resolveFrom(context),
            ),
          ),
          const SizedBox(height: 4),
          Text(
            formatMessageTime(message.created_at),
            style: TextStyle(
              fontSize: 10,
              color: isUserMessage
                  ? CupertinoColors.white.withValues(alpha: 0.5)
                  : CupertinoColors.secondaryLabel
                        .resolveFrom(context)
                        .withValues(alpha: 0.5),
            ),
          ),
        ],
      ),
    );

    return Align(
      alignment: isUserMessage ? .centerRight : .centerLeft,
      child: Padding(
        padding: .only(
          left: isUserMessage ? 40 : 0,
          right: isUserMessage ? 0 : 40,
        ),
        child: messageContent,
      ),
    );
  }

  Widget buildLoadMoreWidget() {
    return Obx(
      () => isLoading.value
          ? const Padding(
              padding: EdgeInsets.symmetric(vertical: 12),
              child: Center(child: CupertinoActivityIndicator()),
            )
          : !hasMore.value
          ? Padding(
              padding: EdgeInsets.symmetric(vertical: 12), //fix：EdgeInsets
              child: Center(
                child: Text(
                  "No more messages",
                  style: TextStyle(
                    color: CupertinoColors.secondaryLabel,
                    fontSize: 14,
                  ),
                ),
              ),
            )
          : const SizedBox.shrink(), //hasmoredatawhenshowcomponent（autoload）
    );
  }

  @override
  Widget build(BuildContext context) {
    return ClipRSuperellipse(
      borderRadius: .circular(12),
      clipBehavior: .antiAliasWithSaveLayer,
      child: CupertinoPageScaffold(
        navigationBar: CupertinoNavigationBar(
          middle: Text("Chat Messages"),
          trailing: CupertinoButton(
            padding: .zero,
            child: Icon(
              CupertinoIcons.xmark_circle_fill,
              size: 25,
              color: CupertinoColors.separator.resolveFrom(context),
            ),
            onPressed: () {
              CupertinoSheetRoute.popSheet(context);
            },
          ),
        ),
        child: Obx(
          () => CustomScrollView(
            controller: scrollController,
            reverse: false,
            slivers: [
              CupertinoSliverRefreshControl(onRefresh: onRefresh),
              SliverToBoxAdapter(child: buildLoadMoreWidget()),
              if (messageList.isEmpty && !isLoading.value)
                //Nulldata
                SliverToBoxAdapter(
                  child: SizedBox(
                    height: MediaQuery.of(context).size.height - 200,
                    child: Center(
                      child: Column(
                        mainAxisAlignment: .center,
                        children: [
                          Icon(
                            CupertinoIcons.chat_bubble,
                            size: 64,
                            color: CupertinoColors.secondaryLabel,
                          ),
                          const SizedBox(height: 16),
                          Text(
                            "No messages in this conversation",
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
              else if (messageList.isEmpty && isLoading.value)
                SliverToBoxAdapter(
                  child: SizedBox(
                    height: MediaQuery.of(context).size.height - 200,
                    child: const Center(
                      child: CupertinoActivityIndicator(radius: 20),
                    ),
                  ),
                )
              else
                SliverPadding(
                  padding: .only(
                    top: MediaQuery.viewPaddingOf(context).top,
                    bottom: MediaQuery.viewPaddingOf(context).bottom + 200,
                  ),
                  sliver: SliverList.separated(
                    itemCount: messageList.length,
                    itemBuilder: (context, index) {
                      return buildMessageItem(messageList[index]);
                    },
                    separatorBuilder: (context, index) =>
                        const SizedBox(height: 8),
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }
}

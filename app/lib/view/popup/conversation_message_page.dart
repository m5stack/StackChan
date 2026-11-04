/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:get/get.dart';
import 'package:intl/intl.dart';
import 'package:stack_chan/model/XiaoZhi/conversation_message_data.dart';
import 'package:stack_chan/util/xiao_zhi_util.dart';

import '../util/popup.dart';
import '../util/theme_setting.dart';

class ConversationMessagePage extends StatefulWidget {
  const ConversationMessagePage({super.key, required this.chatId});

  final int chatId;

  @override
  State<StatefulWidget> createState() => _ConversationMessagePageState();
}

class _ConversationMessagePageState extends State<ConversationMessagePage> {
  RxInt page = RxInt(1);
  int pageSize = 30;

  RxBool isLoading = RxBool(false); // Loading state.
  RxBool hasMore = RxBool(true); // Whether more messages are available.

  final DateFormat timeFormat = DateFormat("yyyy-MM-dd HH:mm"); // Timestamp format.

  RxList<ConversationMessageData> messageList = RxList([]);

  String get _conversationDateFrom {
    final DateTime earliestMessageTime = DateTime(1970, 1, 1);
    return "${earliestMessageTime.year.toString().padLeft(4, '0')}-${earliestMessageTime.month.toString().padLeft(2, '0')}-${earliestMessageTime.day.toString().padLeft(2, '0')}";
  }

  String get _conversationDateTo {
    final DateTime now = DateTime.now();
    return "${now.year.toString().padLeft(4, '0')}-${now.month.toString().padLeft(2, '0')}-${now.day.toString().padLeft(2, '0')}";
  }

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
    final requestPage = page.value;
    try {
      final list = await XiaoZhiUtil.shared.getChatsMessages(
        startDate: _conversationDateFrom,
        endDate: _conversationDateTo,
        chatId: widget.chatId,
        page: requestPage,
        pageSize: pageSize,
      );
      final newData = list.reversed.toList();
      if (isLoadMore) {
        messageList.insertAll(0, newData);
      } else {
        messageList.value = newData;
      }
      hasMore.value = newData.length >= pageSize;
      if (hasMore.value && isLoadMore) {
        page.value = requestPage + 1;
      } else if (!isLoadMore && newData.isNotEmpty) {
        page.value = requestPage + 1;
      }
    } finally {
      isLoading.value = false;
    }
  }

  Future<void> onRefresh() async {
    page.value = 1;
    hasMore.value = true;
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
      padding: const .symmetric(horizontal: 12, vertical: 8),
      margin: const .symmetric(horizontal: 16, vertical: 4),
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
            formatMessageTime(message.createdAt),
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
          ? const Padding(
              padding: EdgeInsets.symmetric(vertical: 12), // Empty-state padding
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
          : const SizedBox.shrink(), // More messages will load automatically on scroll.
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
          controller: scrollController,
          reverse: false,
          slivers: [
            CupertinoSliverNavigationBar(
              backgroundColor:
                  ThemeSetting.cupertinoNavigationBarBackgroundColor(context),
              border: ThemeSetting.cupertinoNavigationBarBorder(context),
              padding: ThemeSetting.navigationBarPadding,
              middle: const Text("Chat Messages"),
              trailing: CupertinoButton(
                sizeStyle: .medium,
                child: const Icon(CupertinoIcons.xmark),
                onPressed: () {
                  closeCustomCupertinoSheet(context);
                },
              ),
            ),
            CupertinoSliverRefreshControl(onRefresh: onRefresh),
            SliverToBoxAdapter(child: buildLoadMoreWidget()),
            if (messageList.isEmpty && !isLoading.value)
              // No messages.
              SliverToBoxAdapter(
                child: SizedBox(
                  height: MediaQuery.of(context).size.height - 200,
                  child: Center(
                    child: Column(
                      mainAxisAlignment: .center,
                      children: [
                        const Icon(
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
                  top: padding.top + kMinInteractiveDimensionCupertino,
                  bottom: padding.bottom + 200,
                  left: padding.left,
                  right: padding.right,
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
    );
  }
}

/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/theme_setting.dart';
import 'package:get/get.dart';
import 'package:webview_flutter/webview_flutter.dart';

class WebPage extends StatefulWidget {
  const WebPage({super.key, required this.url, this.previousPageTitle});

  final String url;
  final String? previousPageTitle;

  @override
  State<StatefulWidget> createState() => _WebPageState();
}

class _WebPageState extends State<WebPage> {
  late WebViewController controller;
  RxString title = "".obs;

  @override
  void initState() {
    super.initState();
    controller = WebViewController()
      ..setJavaScriptMode(JavaScriptMode.unrestricted)
      ..setNavigationDelegate(
        NavigationDelegate(
          onHttpAuthRequest: (HttpAuthRequest request) {},
          onPageFinished: (String url) async {
            title.value = (await controller.getTitle()) ?? "";
          },
        ),
      )
      ..loadRequest(Uri.parse(widget.url));
  }

  @override
  Widget build(BuildContext context) {
    final padding = MediaQuery.paddingOf(context);

    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      navigationBar: CupertinoNavigationBar(
        backgroundColor: ThemeSetting.cupertinoNavigationBarBackgroundColor(
          context,
        ),
        border: ThemeSetting.cupertinoNavigationBarBorder(context),
        padding: ThemeSetting.navigationBarPadding,
        middle: Obx(() => Text(title.value, maxLines: 1, overflow: .ellipsis)),
        leading: CupertinoButton(
          sizeStyle: .medium,
          onPressed: () {
            Navigator.maybePop(context);
          },
          child: const Icon(CupertinoIcons.back),
        ),
        trailing: CupertinoButton(
          sizeStyle: .medium,
          onPressed: () {
            controller.reload();
          },
          child: const Icon(CupertinoIcons.refresh),
        ),
      ),
      child: Padding(
        padding: .only(left: padding.left, right: padding.right),
        child: WebViewWidget(controller: controller),
      ),
    );
  }
}

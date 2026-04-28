/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';
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
    return CupertinoPageScaffold(
      navigationBar: CupertinoNavigationBar(
        padding: .only(start: 8, end: 8),
        middle: Obx(() => Text(title.value, maxLines: 1, overflow: .ellipsis)),
        leading: CupertinoNavigationBarBackButton(
          previousPageTitle: widget.previousPageTitle,
        ),
        trailing: CupertinoButton(
          padding: .zero,
          child: Icon(CupertinoIcons.refresh),
          onPressed: () {
            controller.reload();
          },
        ),
      ),
      child: WebViewWidget(controller: controller),
    );
  }
}

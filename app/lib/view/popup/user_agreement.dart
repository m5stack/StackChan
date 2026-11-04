/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:io';

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/theme_setting.dart';
import 'package:flutter/services.dart';
import 'package:liquid_glass_widgets/liquid_glass_widgets.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/network/urls.dart';
import 'package:stack_chan/util/value_constant.dart';
import 'package:webview_flutter/webview_flutter.dart';

class UserAgreementPopup extends StatefulWidget {
  const UserAgreementPopup({super.key});

  @override
  State<StatefulWidget> createState() => _UserAgreementPopupState();
}

class _UserAgreementPopupState extends State<UserAgreementPopup>
    with WidgetsBindingObserver {
  late WebViewController webViewController;
  bool _closed = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    webViewController = WebViewController()
      ..setJavaScriptMode(JavaScriptMode.unrestricted)
      ..loadRequest(Uri.parse(Urls.privacyPolicyUrl));
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    super.didChangeAppLifecycleState(state);
    if (state == AppLifecycleState.resumed && Platform.isIOS) {
      setState(() {
        webViewController.loadRequest(Uri.parse(Urls.privacyPolicyUrl));
      });
    }
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  /// Closes the agreement popup.
  ///
  /// This must use [Navigator.pop] rather than [Navigator.maybePop]:
  /// the popup wraps a `PopScope(canPop: false)` to intercept the system back
  /// gesture, and `maybePop` consults that route's popDisposition first, gets
  /// doNotPop back and returns immediately - leaving the button with no
  /// visible effect. `pop` bypasses that interception entirely.
  void _close() {
    if (_closed || !mounted) {
      return;
    }
    final route = ModalRoute.of(context);
    if (route == null || !route.isCurrent) {
      return;
    }
    _closed = true;
    Navigator.of(context, rootNavigator: true).pop();
  }

  Future<void> _onAgree() async {
    try {
      await AppState.asyncPrefs.setBool(ValueConstant.agreeUserAgreement, true);
    } catch (e, stack) {
      // Don't block the user if persisting fails; we'll ask again next launch.
      debugPrint('Failed to save user agreement: $e\n$stack');
    }
    _close();
  }

  void _onRefuse() {
    _close();
    if (Platform.isIOS) {
      exit(0);
    } else {
      SystemNavigator.pop();
    }
  }

  @override
  Widget build(BuildContext context) {
    final padding = MediaQuery.paddingOf(context);

    return PopScope(
      canPop: false,
      child: CupertinoPageScaffold(
        backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
          context,
        ),
        navigationBar: CupertinoNavigationBar.large(
          backgroundColor: ThemeSetting.cupertinoNavigationBarBackgroundColor(
            context,
          ),
          border: ThemeSetting.cupertinoNavigationBarBorder(context),
          padding: ThemeSetting.navigationBarPadding,
          largeTitle: const Text("Privacy Policy"),
        ),
        child: Padding(
          padding: .only(left: padding.left, right: padding.right),
          child: Column(
            children: [
              Expanded(child: WebViewWidget(controller: webViewController)),
              Padding(
                padding: .only(
                  left: 20,
                  right: 20,
                  top: 20,
                  bottom: padding.bottom + 20,
                ),
                child: Row(
                  spacing: 15,
                  children: [
                    Expanded(
                      child: GlassButton.custom(
                        height: 50,
                        settings: LiquidGlassSettings(
                          glassColor: CupertinoColors.destructiveRed
                              .resolveFrom(context)
                              .withValues(alpha: 0.2),
                        ),
                        shape: const LiquidRoundedSuperellipse(
                          borderRadius: 23,
                        ),
                        onTap: _onRefuse,
                        child: Center(
                          child: Text(
                            "Refuse",
                            style: TextStyle(
                              color: CupertinoColors.destructiveRed.resolveFrom(
                                context,
                              ),
                            ),
                          ),
                        ),
                      ),
                    ),
                    Expanded(
                      child: GlassButton.custom(
                        height: 50,
                        settings: LiquidGlassSettings(
                          glassColor: CupertinoTheme.of(context).primaryColor,
                        ),
                        shape: const LiquidRoundedSuperellipse(
                          borderRadius: 23,
                        ),
                        onTap: _onAgree,
                        child: const Center(
                          child: Text(
                            "Agree",
                            style: TextStyle(color: CupertinoColors.white),
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
      ),
    );
  }
}

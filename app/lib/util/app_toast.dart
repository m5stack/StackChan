/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';

import 'package:flutter/cupertino.dart';
import 'package:stack_chan/app_state.dart';

class AppToast extends StatefulWidget {
  const AppToast({super.key, this.child});

  final Widget? child;

  @override
  State<AppToast> createState() => _AppToastState();
}

class _AppToastState extends State<AppToast> {
  String _toastText = "";
  bool _isShowToast = false;

  //Replace with standard Timer, fix timer invalidation issue
  Timer? _hideTimer;

  static const _duration = Duration(milliseconds: 1500);
  static const _fadeDuration = Duration(milliseconds: 200);

  @override
  void initState() {
    super.initState();
    //listenAndsaveSubscribe
    AppState.shared.toastFunction = (toastText) {
      if (toastText == null || toastText.isEmpty) {
        _hideToast();
      } else {
        _updateToast(toastText);
      }
    };
  }

  @override
  void dispose() {
    AppState.shared.toastFunction = null;
    _hideTimer?.cancel();
    super.dispose();
  }

  ///Update Toast text and show
  void _updateToast(String text) {
    //First / Previouslycancel旧timer,avoidRepeat计when
    _hideTimer?.cancel();
    setState(() {
      _toastText = text;
      _isShowToast = true;
    });
    //startnewautohidetimer
    _hideTimer = Timer(_duration, _hideToast);
  }

  ///Hide Toast
  void _hideToast() {
    if (mounted) {
      //Avoid setState error when page is already disposed
      setState(() {
        _isShowToast = false;
      });
    }
    _hideTimer?.cancel();
    _hideTimer = null;
  }

  ///Build Toast style (remove redundant Visibility, use only AnimatedOpacity)
  Widget _buildToastWidget() {
    return Positioned(
      top: 80,
      left: 0,
      right: 0,
      child: Center(
        child: AnimatedOpacity(
          opacity: _isShowToast ? 1.0 : 0.0,
          duration: _fadeDuration,
          child: Container(
            padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
            decoration: BoxDecoration(
              color: CupertinoColors.secondarySystemGroupedBackground
                  .resolveFrom(context)
                  .withValues(alpha: 0.7),
              borderRadius: BorderRadius.circular(12),
              boxShadow: [
                BoxShadow(
                  color: CupertinoColors.systemBackground
                      .resolveFrom(context)
                      .withValues(alpha: 0.1),
                  blurRadius: 12,
                  offset: const Offset(0, 2),
                ),
              ],
            ),
            child: Text(
              _toastText,
              style: TextStyle(
                color: CupertinoColors.label.resolveFrom(context),
                fontSize: 16,
              ),
              textAlign: TextAlign.center,
            ),
          ),
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Stack(
      children: [if (widget.child != null) widget.child!, _buildToastWidget()],
    );
  }
}

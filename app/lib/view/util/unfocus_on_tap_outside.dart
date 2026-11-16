/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/gestures.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

/// Dismisses the keyboard when the user taps outside a text field.
///
/// A [Listener] is used instead of a full-screen [GestureDetector]:
/// * [Listener] only observes pointer events and does not join the gesture
///   arena, so it never competes with buttons, scroll views, or native
///   platform views such as the iOS 26 `UITabBar` or `UiKitView`.
/// * The keyboard is dismissed only on a tap; drags (scrolling, text
///   selection, or dragging a selection handle) are ignored.
/// * Taps inside a text field (hitting a [RenderEditable]) leave focus alone,
///   so the keyboard does not close and immediately reopen.
///
/// To unfocus a single field on an outside tap, prefer its own
/// `onTapOutside` handler or [TapRegion]. The global behavior is kept here so
/// that individual fields do not have to be changed one by one.
class UnfocusOnTapOutside extends StatefulWidget {
  const UnfocusOnTapOutside({super.key, required this.child});

  final Widget child;

  @override
  State<UnfocusOnTapOutside> createState() => _UnfocusOnTapOutsideState();
}

class _UnfocusOnTapOutsideState extends State<UnfocusOnTapOutside> {
  int? _pointer;
  Offset? _downPosition;
  bool _startedInEditable = false;
  bool _moved = false;

  @override
  Widget build(BuildContext context) {
    return Listener(
      behavior: HitTestBehavior.translucent,
      onPointerDown: _handlePointerDown,
      onPointerMove: _handlePointerMove,
      onPointerUp: _handlePointerUp,
      onPointerCancel: _handlePointerCancel,
      child: widget.child,
    );
  }

  void _handlePointerDown(PointerDownEvent event) {
    if (_pointer != null) return;
    if (FocusManager.instance.primaryFocus == null) return;

    _pointer = event.pointer;
    _downPosition = event.position;
    _moved = false;
    _startedInEditable = _isInsideEditable(event);
  }

  void _handlePointerMove(PointerMoveEvent event) {
    if (event.pointer != _pointer || _moved) return;
    final downPosition = _downPosition;
    if (downPosition != null &&
        (event.position - downPosition).distance > kTouchSlop) {
      _moved = true;
    }
  }

  void _handlePointerUp(PointerUpEvent event) {
    if (event.pointer != _pointer) return;

    final shouldUnfocus = !_moved && !_startedInEditable;
    _reset();
    if (shouldUnfocus) {
      FocusManager.instance.primaryFocus?.unfocus();
    }
  }

  void _handlePointerCancel(PointerCancelEvent event) {
    if (event.pointer == _pointer) _reset();
  }

  void _reset() {
    _pointer = null;
    _downPosition = null;
    _startedInEditable = false;
    _moved = false;
  }

  bool _isInsideEditable(PointerDownEvent event) {
    final result = HitTestResult();
    WidgetsBinding.instance.hitTestInView(result, event.position, event.viewId);
    return result.path.any((entry) => entry.target is RenderEditable);
  }
}

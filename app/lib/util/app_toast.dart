/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:ui';

import 'package:cupertino_ui/cupertino_ui.dart';

/// Global toast overlay.
class GlobalToast extends StatefulWidget {
  const GlobalToast({super.key, this.child});

  final Widget? child;

  static final ValueNotifier<_ToastRequest?> _requestNotifier =
      ValueNotifier<_ToastRequest?>(null);
  static int _requestId = 0;

  static void show({
    String? title,
    String? message,
    Duration duration = const Duration(seconds: 3),
    Duration debounceDuration = const Duration(milliseconds: 160),
  }) {
    if (!_hasText(title) && !_hasText(message)) return;
    _requestNotifier.value = _ToastRequest(
      id: _requestId++,
      title: title,
      message: message,
      duration: duration,
      debounceDuration: debounceDuration,
    );
  }

  static bool _hasText(String? value) {
    return value != null && value.trim().isNotEmpty;
  }

  @override
  State<GlobalToast> createState() => _GlobalToastState();
}

class _GlobalToastState extends State<GlobalToast> {
  Timer? _debounceTimer;
  Timer? _dismissTimer;
  _ToastRequest? _activeToast;
  _ToastRequest? _pendingToast;

  @override
  void initState() {
    super.initState();
    GlobalToast._requestNotifier.addListener(_handleToastRequest);
  }

  @override
  void dispose() {
    GlobalToast._requestNotifier.removeListener(_handleToastRequest);
    _debounceTimer?.cancel();
    _dismissTimer?.cancel();
    super.dispose();
  }

  void _handleToastRequest() {
    final request = GlobalToast._requestNotifier.value;
    if (request == null) return;

    _pendingToast = request;
    _debounceTimer?.cancel();
    if (request.debounceDuration <= Duration.zero) {
      _showToast(request);
      return;
    }

    _debounceTimer = Timer(request.debounceDuration, () {
      if (!mounted || _pendingToast != request) return;
      _showToast(request);
    });
  }

  void _showToast(_ToastRequest request) {
    _dismissTimer?.cancel();
    setState(() {
      _activeToast = request;
    });

    if (request.duration <= Duration.zero) return;
    _dismissTimer = Timer(request.duration, () {
      if (!mounted) return;
      _dismissToast(request);
    });
  }

  void _dismissToast([_ToastRequest? request]) {
    if (request != null && _activeToast != request) return;
    _dismissTimer?.cancel();
    _dismissTimer = null;
    if (_activeToast == null) return;
    setState(() {
      _activeToast = null;
    });
  }

  @override
  Widget build(BuildContext context) {
    final activeToast = _activeToast;
    return Stack(
      fit: StackFit.expand,
      children: [
        widget.child ?? const SizedBox.expand(),
        Positioned(
          left: 20,
          right: 20,
          top: MediaQuery.paddingOf(context).top + 20,
          child: IgnorePointer(
            ignoring: _activeToast == null,
            child: activeToast == null
                ? const SizedBox.shrink(key: ValueKey('empty-toast'))
                : _GlobalToastCard(
                    key: ValueKey(activeToast.id),
                    toast: activeToast,
                    onDismiss: () => _dismissToast(activeToast),
                  ),
          ),
        ),
      ],
    );
  }
}

class _GlobalToastCard extends StatelessWidget {
  const _GlobalToastCard({
    required this.toast,
    required this.onDismiss,
    super.key,
  });

  final _ToastRequest toast;
  final VoidCallback onDismiss;

  @override
  Widget build(BuildContext context) {
    final title = toast.title;
    final message = toast.message;
    final hasTitle = GlobalToast._hasText(title);
    final hasMessage = GlobalToast._hasText(message);
    final textTheme = CupertinoTheme.of(context).textTheme;
    final shadowColor = CupertinoColors.systemFill
        .resolveFrom(context)
        .withValues(alpha: 0.2);

    return Semantics(
      liveRegion: true,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: onDismiss,
        child: DecoratedBox(
          decoration: BoxDecoration(
            borderRadius: .circular(23),
            boxShadow: [BoxShadow(color: shadowColor, blurRadius: 12)],
          ),
          child: ClipRSuperellipse(
            clipBehavior: Clip.antiAlias,
            borderRadius: .circular(23),
            child: BackdropFilter(
              filter: ImageFilter.blur(sigmaX: 12, sigmaY: 12),
              child: DecoratedBox(
                decoration: BoxDecoration(
                  color: CupertinoColors.tertiarySystemBackground
                      .resolveFrom(context)
                      .withValues(alpha: 0.5),
                ),
                child: Padding(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 20,
                    vertical: 20,
                  ),
                  child: SizedBox(
                    width: double.infinity,
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        if (hasTitle)
                          Text(
                            title!,
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: textTheme.navTitleTextStyle,
                          ),
                        if (hasTitle && hasMessage) const SizedBox(height: 4),
                        if (hasMessage)
                          Text(
                            message!,
                            maxLines: 3,
                            overflow: TextOverflow.ellipsis,
                            style: textTheme.textStyle,
                          ),
                      ],
                    ),
                  ),
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _ToastRequest {
  const _ToastRequest({
    required this.id,
    required this.duration,
    required this.debounceDuration,
    this.title,
    this.message,
  });

  final int id;
  final String? title;
  final String? message;
  final Duration duration;
  final Duration debounceDuration;
}

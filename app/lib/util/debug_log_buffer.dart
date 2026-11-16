/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/foundation.dart';

class DebugLogBuffer {
  static const int _maxLines = 2000;

  static final List<String> _lines = [];
  static DebugPrintCallback? _previousDebugPrint;
  static bool _installed = false;

  static void install() {
    if (!kDebugMode || _installed) return;

    _installed = true;
    _previousDebugPrint = debugPrint;
    debugPrint = (String? message, {int? wrapWidth}) {
      _append(message);
      _previousDebugPrint?.call(message, wrapWidth: wrapWidth);
    };
  }

  static String get text => _lines.join('\n');

  static void _append(String? message) {
    final timestamp = DateTime.now().toIso8601String();
    final logLines = (message ?? 'null').split('\n');

    for (final line in logLines) {
      _lines.add('[$timestamp] $line');
    }

    final overflow = _lines.length - _maxLines;
    if (overflow > 0) {
      _lines.removeRange(0, overflow);
    }
  }
}

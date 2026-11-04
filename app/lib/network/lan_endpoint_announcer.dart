/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';

import 'package:flutter/foundation.dart' show visibleForTesting;
import 'package:stack_chan/network/local_area_network_service.dart';

/// Announces the phone's LAN endpoint to the firmware over the remote channel.
///
/// Endpoint readiness, remote reconnects, and device-online events trigger immediate announcements.
/// Until the target connects directly, resend every 3 seconds so firmware entering AVATAR
/// late still receives the endpoint; stop once connected to save bandwidth, and resume on disconnect.
///
/// Startup order therefore does not matter: device-online and page entry trigger immediate
/// announcements, while the 3-second retry covers late rounds.
class LanEndpointAnnouncer {
  LanEndpointAnnouncer({
    required this.send,
    this.keepAliveInterval = const Duration(seconds: 3),
  });

  /// Sends `AppLanAddress`; the caller drops the update when the remote channel is unavailable.
  final void Function(LanServerEndpoint endpoint) send;

  /// Resend interval while no direct connection is established.
  final Duration keepAliveInterval;

  LanServerEndpoint? _endpoint;
  Timer? _keepAlive;
  bool _lanConnected = false;
  bool _disposed = false;

  @visibleForTesting
  bool get isKeepAliveRunning => _keepAlive != null;

  @visibleForTesting
  LanServerEndpoint? get endpoint => _endpoint;

  /// Called when the LAN service starts or stops; null stops further announcements.
  void updateEndpoint(LanServerEndpoint? endpoint) {
    if (_disposed || identical(_endpoint, endpoint)) return;
    _endpoint = endpoint;
    if (endpoint == null) {
      _stopKeepAlive();
      return;
    }
    announce();
  }

  /// Announces immediately after a remote reconnect or when a device comes online.
  void announce() {
    if (_disposed) return;
    _sendNow();
    _syncKeepAlive();
  }

  /// Updates direct-connection state: stop resending once connected and resume immediately after disconnect.
  void setLanConnected(bool connected) {
    if (_disposed || _lanConnected == connected) return;
    _lanConnected = connected;
    if (connected) {
      _stopKeepAlive();
      return;
    }
    announce();
  }

  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _stopKeepAlive();
  }

  void _sendNow() {
    final endpoint = _endpoint;
    if (endpoint != null) send(endpoint);
  }

  void _syncKeepAlive() {
    if (_disposed || _endpoint == null || _lanConnected) {
      _stopKeepAlive();
      return;
    }
    _keepAlive ??= Timer.periodic(keepAliveInterval, (_) => _sendNow());
  }

  void _stopKeepAlive() {
    _keepAlive?.cancel();
    _keepAlive = null;
  }
}

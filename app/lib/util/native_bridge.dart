/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';

import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';

class NativeBridge {
  static final NativeBridge shared = NativeBridge._internal();

  NativeBridge._internal() {
    _channel.setMethodCallHandler(_handleMethodCall);
  }

  final MethodChannel _channel = MethodChannel("com.m5stack.stackchan/native");

  final BasicMessageChannel _audioPlayChannel = const BasicMessageChannel(
    "com.m5stack.stackchan/audio_play",
    BinaryCodec(),
  );

  // final EventChannel recordChannel = const EventChannel(
  //   "com.m5stack.stackchan/record",
  // );

  final Map<Method, Future<dynamic> Function(MethodCall)> _handlers = {};

  /// Register a handler for a specific native method
  void registerHandler(
    Method method,
    Future<dynamic> Function(MethodCall) handler,
  ) {
    _handlers[method] = handler;
  }

  /// Register multiple handlers at once
  void registerHandlers(
    Map<Method, Future<dynamic> Function(MethodCall)> handlers,
  ) {
    _handlers.addAll(handlers);
  }

  /// Unregister a handler for a specific native method
  void unregisterHandler(Method method) {
    _handlers.remove(method);
  }

  /// Unregister multiple handlers at once
  void unregisterHandlers(List<Method> methods) {
    for (final method in methods) {
      _handlers.remove(method);
    }
  }

  /// Unregister all handlers
  void unregisterAllHandlers() {
    _handlers.clear();
  }

  /// Check if a handler is registered for a specific method
  bool hasHandler(Method method) {
    return _handlers.containsKey(method);
  }

  /// Get all registered method names
  List<Method> get registeredMethods => _handlers.keys.toList();

  Future<dynamic> _handleMethodCall(MethodCall call) async {
    try {
      final method = Method.fromString(call.method);
      if (method != Method.unknown && _handlers.containsKey(method)) {
        return await _handlers[method]!(call);
      } else {
        debugPrint('Unregistered method: ${call.method}');
        return null;
      }
    } catch (e) {
      debugPrint('Error handling method call: $e');
      return null;
    }
  }

  /// Send message to native side with optional arguments
  Future<dynamic> sendMessage(Method method, [dynamic arguments]) async {
    try {
      return await _channel.invokeMethod(method.name, arguments);
    } catch (e) {
      debugPrint('Error sending message to native: $e');
      return null;
    }
  }

  /// Send PCM audio stream to native side for playback
  Future<dynamic> sendAudioStream(ByteData pcmData) async {
    try {
      await _audioPlayChannel.send(pcmData);
    } catch (e) {
      debugPrint("Audio stream send failed: $e");
    }
  }
}

enum Method {
  wifiName,
  stopPlayPCM,
  startRecording,
  stopRecording,
  unknown;

  static Method fromString(String name) {
    return Method.values.firstWhere(
      (m) => m.name == name,
      orElse: () => Method.unknown,
    );
  }
}

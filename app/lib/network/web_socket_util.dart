/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:flutter/cupertino.dart';

//Assume theseisYoucustomDepends on(according toActual situationAdjust)
import '../app_state.dart';
import '../model/msg_type.dart';
import '../util/rsa_util.dart';
import '../util/value_constant.dart';

class WebSocketUtil {
  WebSocketUtil._internal();

  static final WebSocketUtil shared = WebSocketUtil._internal();

  WebSocket? _socket;
  StreamSubscription? _subscription;

  //newIncrease / Add:Recordcurrentconnectstate,avoidRepeatPrint
  bool _isConnected = false;

  Function()? connectionSuccessful;

  final Map<String, void Function(dynamic)> _observers = {};

  String _urlString = '';

  /* =======================
   * Authorization
   * ======================= */
  String getAuthorization(String mac) {
    final rand = Random();
    final randomPart = List.generate(
      mac.length,
      (_) =>
          ValueConstant.characters[rand.nextInt(
            ValueConstant.characters.length,
          )],
    ).join();

    final timestamp = DateTime.now().millisecondsSinceEpoch ~/ 1000;
    return '$mac|$randomPart|$timestamp';
  }

  /* =======================
   * Connect
   * ======================= */
  Future<void> connect(String urlString) async {
    //ifalreadyexistconnect,First / PreviouslyDisconnect
    if (_socket != null) {
      disconnect();
    }

    _urlString = urlString;

    if (AppState.shared.deviceMac.isEmpty) {
      debugPrint(' WebSocket ConnectFailed:DeviceMACAddressIs null/empty');
      return;
    }

    //Printconnectstartlog
    debugPrint('🔌 StartConnect WebSocket: $urlString');

    try {
      final encryptedToken = RsaUtil.encrypt(
        getAuthorization(AppState.shared.deviceMac),
      );
      final headers = {ValueConstant.authorization: encryptedToken};
      _socket = await WebSocket.connect(urlString, headers: headers);

      //connectsuccesslog(ContainstimeandURL)
      _isConnected = true;
      final connectTime = DateTime.now().toString().split('.').first;
      debugPrint(' WebSocket ConnectSuccess [$connectTime]');
      debugPrint('   ConnectAddress: $urlString');
      debugPrint('   ConnectState: ${_socket?.readyState} (OPEN)');

      _subscription = _socket!.listen(
        _handleMessage,
        onError: _handleError,
        onDone: _handleDone,
        cancelOnError: true,
      );

      if (connectionSuccessful != null) {
        connectionSuccessful!();
      }
    } catch (e) {
      _isConnected = false;
      //connectfaillog(ContainsSpecificerrorinfo)
      final errorTime = DateTime.now().toString().split('.').first;
      debugPrint(' WebSocket ConnectFailed [$errorTime]');
      debugPrint('   ConnectAddress: $urlString');
      debugPrint('   Error原Because: $e');
      _scheduleReconnect();
    }
  }

  /* =======================
   * Message Handling
   * ======================= */
  void _handleMessage(dynamic message) {
    final isPing = replyPong(message);
    if (!isPing) {
      _notifyObservers(message);
    }
  }

  void _handleError(Object error) {
    //errorlog(DistinguishconnecterrorandRunning / Runtimewhenerror)
    debugPrint(' WebSocket Running / RuntimeWhenError: $error');
    _isConnected = false;
    _scheduleReconnect();
  }

  void _handleDone() {
    //connectcloselog(ContainscloseoriginalBecause)
    _isConnected = false;
    final closeTime = DateTime.now().toString().split('.').first;
    debugPrint(' WebSocket ConnectAlready关闭 [$closeTime]');
    debugPrint('   关闭Address: $_urlString');
    debugPrint('   关闭State: ${_socket?.closeCode} - ${_socket?.closeReason}');
    _scheduleReconnect();
  }

  bool replyPong(dynamic message) {
    if (message is Uint8List) {
      final result = AppState.shared.parseMessage(message);
      final msgType = result.$1;

      if (msgType != null) {
        switch (msgType) {
          case MsgType.ping:
            AppState.shared.sendWebSocketMessage(.pong);
            return true;
          default:
            return false;
        }
      }
    }
    return false;
  }

  /* =======================
   * Send
   * ======================= */
  void sendString(String message) {
    if (_socket == null) {
      debugPrint(' 发送StringMessageFailed:WebSocket Disconnected');
      return;
    }

    debugPrint('📤 发送StringMessage: $message');
    try {
      _socket!.add(message);
    } catch (e) {
      debugPrint(' 发送StringMessageFailed: $e');
      _isConnected = false;
      _scheduleReconnect();
    }
  }

  void send(Uint8List data) {
    if (_socket == null) {
      debugPrint(' 发送2BaseMessageFailed:WebSocket Disconnected');
      return;
    }

    //debugPrint('📤 send2Basemessage: length=${data.length} Byte');
    try {
      _socket!.add(data);
    } catch (e) {
      debugPrint(' 发送2BaseMessageFailed: $e');
      _isConnected = false;
      _scheduleReconnect();
    }
  }

  /* =======================
   * Reconnect
   * ======================= */
  void _scheduleReconnect() async {
    if (_urlString.isEmpty) return;

    //reconnectlog(avoidFrequentlyRepeatPrint)
    if (!_isConnected) {
      debugPrint('🔄 准备重连 WebSocket: $_urlString (1Second(s)BackRetry)');
    }

    await Future.delayed(const Duration(seconds: 1));
    await connect(_urlString);
  }

  /* =======================
   * Disconnect
   * ======================= */
  void disconnect() {
    _subscription?.cancel();
    _socket?.close(WebSocketStatus.goingAway, '主动断开连接');
    _isConnected = false;
    _socket = null;
    debugPrint('🔌 WebSocket AlreadyProactiveDisconnectConnect');
  }

  /* =======================
   * Observer
   * ======================= */
  void addObserver(String key, void Function(dynamic message) observer) {
    _observers[key] = observer;
  }

  void removeObserver(String key) {
    _observers.remove(key);
  }

  void removeAllObservers() {
    _observers.clear();
  }

  void _notifyObservers(dynamic message) {
    for (final observer in _observers.values) {
      observer(message);
    }
  }

  //newIncrease / Add:Getcurrentconnectstatemethod(ConvenientExternalquery)
  bool get isConnected => _isConnected && _socket?.readyState == WebSocket.open;
}

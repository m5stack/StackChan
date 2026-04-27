/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:convert';
import 'dart:io';

import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';
import 'package:stack_chan/model/dance_list.dart';

class StackChanArView extends StatefulWidget {
  const StackChanArView({
    super.key,
    required this.decorate,
    required this.captureScreen,
    this.onFrameCallback,
    this.onCallback,
  });

  final int decorate; //脸上玩具还ismodel
  final bool captureScreen; //output画面
  final Function(Uint8List)? onFrameCallback; //画面outputcallback
  final Function(DanceData)? onCallback; //检测datacallback

  @override
  State<StatefulWidget> createState() => _StackChanArViewState();
}

class _StackChanArViewState extends State<StackChanArView> {
  final String viewType = "stackchan_ar_view";
  final String methodChannelName = "com.stackchan.ar.view";
  int? viewId;
  late MethodChannel methodChannel;
  late EventChannel expressionChannel;
  late EventChannel frameChannel;

  void initializeChannels(int id) {
    String methodName = "${methodChannelName}_${id.toString()}";
    methodChannel = MethodChannel(methodName);
    expressionChannel = EventChannel("${methodName}_expression");
    frameChannel = EventChannel("${methodName}_frame");
  }

  void registerExpressionCallback() {
    if (widget.onCallback != null) {
      expressionChannel.receiveBroadcastStream().listen((event) {
        final danceData = DanceData.fromJson(jsonDecode(event));
        widget.onCallback!(danceData);
      });
    }
  }

  void registerFrameCallback() {
    if (widget.onFrameCallback != null) {
      frameChannel.receiveBroadcastStream().listen((event) {
        if (event is Uint8List) {
          widget.onFrameCallback!(event);
        }
      });
    }
  }

  @override
  void didUpdateWidget(covariant StackChanArView oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.onCallback != widget.onCallback ||
        oldWidget.onFrameCallback != widget.onFrameCallback) {
      registerCallbacks();
    }
    if (oldWidget.decorate != widget.decorate) {
      setDecorate();
    }
    if (oldWidget.captureScreen != widget.captureScreen) {
      setCaptureScreen();
    }
  }

  void registerCallbacks() {
    registerExpressionCallback();
    registerFrameCallback();
  }

  @override
  void dispose() {
    disposeNative();
    super.dispose();
  }

  Future<void> disposeNative() async {
    if (viewId == null) return;
    try {
      await methodChannel.invokeMethod("dispose");
      methodChannel.setMethodCallHandler(null);
    } catch (_) {}
  }

  Future<void> setDecorate() async {
    if (viewId == null) return;
    await methodChannel.invokeMethod("setDecorate", widget.decorate);
  }

  Future<void> setCaptureScreen() async {
    if (viewId == null) return;
    await methodChannel.invokeMethod("setCaptureScreen", widget.captureScreen);
  }

  @override
  Widget build(BuildContext context) {
    if (Platform.isIOS) {
      return UiKitView(
        viewType: viewType,
        creationParamsCodec: const StandardMessageCodec(),
        onPlatformViewCreated: onPlatformViewCreated,
      );
    } else {
      return Center(child: Text("暂不支持"));
    }
  }

  void onPlatformViewCreated(int id) {
    viewId = id;
    initializeChannels(id);
    registerCallbacks();
    setCaptureScreen();
    setDecorate();
  }
}

/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/cupertino.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/services.dart';

import '../../model/dance_list.dart';

class StackChanRobot extends StatefulWidget {
  final DanceData data;

  final double width;
  final double height;

  final bool? topLook;
  final bool? allowsCameraControl;

  const StackChanRobot({
    super.key,
    required this.data,
    required this.width,
    required this.height,
    this.topLook,
    this.allowsCameraControl,
  });

  @override
  State<StatefulWidget> createState() => _StackchanRobotState();
}

class _StackchanRobotState extends State<StackChanRobot> {
  late MethodChannel _methodChannel;
  int? _viewId;

  final String _viewType = "stackchan_robot_view";
  final String _methodChannelName = "com.stackchan.robot.method";

  @override
  void didUpdateWidget(covariant StackChanRobot oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (_viewId == null) return;
    _updateDanceData();
    if (widget.topLook != oldWidget.topLook) {
      _setTopLook(widget.topLook);
    }
    if (widget.allowsCameraControl != oldWidget.allowsCameraControl) {
      _setAllowsCameraControl(widget.allowsCameraControl);
    }
  }

  void _initializeChannels(int id) {
    String methodName = "${_methodChannelName}_${id.toString()}";
    _methodChannel = MethodChannel(methodName);
  }

  void _updateDanceData() async {
    if (_viewId == null) return;
    await _methodChannel.invokeMethod(
      "updateDanceData",
      jsonEncode(widget.data.toJson()),
    );
  }

  Future<void> _setTopLook(bool? value) async {
    if (_viewId == null) return;
    await _methodChannel.invokeMethod("setTopLook", value ?? false);
  }

  Future<void> _setAllowsCameraControl(bool? value) async {
    if (_viewId == null) return;
    await _methodChannel.invokeMethod("setAllowsCameraControl", value ?? false);
  }

  Future<void> _disposeNative() async {
    if (_viewId == null) return;
    try {
      await _methodChannel.invokeMethod("dispose");
    } catch (_) {}
  }

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: widget.width,
      height: widget.height,
      child: _buildNativeView(),
    );
  }

  Widget _buildNativeView() {
    return LayoutBuilder(
      builder: (context, constraints) {
        if (Platform.isIOS) {
          return UiKitView(
            viewType: _viewType,
            creationParamsCodec: const StandardMessageCodec(),
            creationParams: widget.data.toJson(),
            onPlatformViewCreated: _onPlatformViewCreated,
          );
        } else if (Platform.isAndroid) {
          return Image.asset("assets/image1.png");
        } else {
          return Center(child: Text("暂时不支持"));
        }
      },
    );
  }

  void _onPlatformViewCreated(int id) {
    _viewId = id;
    _initializeChannels(id);
    _updateDanceData();
    _setTopLook(widget.topLook);
    _setAllowsCameraControl(widget.allowsCameraControl);
  }

  @override
  void dispose() {
    _disposeNative();
    super.dispose();
  }
}

class StackChanRotary extends StatelessWidget {
  const StackChanRotary({super.key, required this.width, required this.height});

  final double width;
  final double height;

  final String _viewType = "stackchan_rotary_robot_view";

  @override
  Widget build(BuildContext context) {
    return SizedBox(width: width, height: height, child: _buildNativeView());
  }

  Widget _buildNativeView() {
    if (Platform.isIOS) {
      return UiKitView(
        viewType: _viewType,
        creationParamsCodec: const StandardMessageCodec(),
      );
    } else if (Platform.isAndroid) {
      return PlatformViewLink(
        surfaceFactory: (context, controller) {
          return AndroidViewSurface(
            controller: controller as AndroidViewController,
            hitTestBehavior: .opaque,
            gestureRecognizers: const <Factory<OneSequenceGestureRecognizer>>{},
          );
        },
        onCreatePlatformView: (params) {
          return PlatformViewsService.initSurfaceAndroidView(
              id: params.id,
              viewType: _viewType,
              layoutDirection: .ltr,
              creationParamsCodec: const StandardMessageCodec(),
              onFocus: () {
                params.onFocusChanged(true);
              },
            )
            ..addOnPlatformViewCreatedListener(params.onPlatformViewCreated)
            ..create();
        },
        viewType: _viewType,
      );
    } else {
      return Center(child: Text("暂时不支持"));
    }
  }
}

/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';
import 'package:video_player/video_player.dart';

class LoopPlaybackVideo extends StatefulWidget {
  const LoopPlaybackVideo({super.key, required this.url});

  final String url;

  @override
  State<StatefulWidget> createState() => _LoopPlaybackVideoState();
}

class _LoopPlaybackVideoState extends State<LoopPlaybackVideo> {
  late VideoPlayerController _controller;

  @override
  void initState() {
    super.initState();

    _controller = VideoPlayerController.asset(
      widget.url,
      videoPlayerOptions: VideoPlayerOptions(mixWithOthers: true),
    );

    _controller.initialize().then((_) {
      setState(() {});
      _controller.setVolume(0);
      _controller.setLooping(true);
      _controller.play();
    });
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!_controller.value.isInitialized) {
      return const Center(child: CupertinoActivityIndicator());
    }
    return AspectRatio(
      aspectRatio: _controller.value.aspectRatio,
      child: VideoPlayer(_controller),
    );
  }
}

/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:math';
import 'dart:typed_data';

import 'package:flutter/cupertino.dart';
import 'package:opus_codec/opus_codec.dart' as opus_flutter;
import 'package:opus_codec_dart/opus_codec_dart.dart';
import 'package:permission_handler/permission_handler.dart';

import 'native_bridge.dart';

class AudioEngineManager {
  static final AudioEngineManager shared = AudioEngineManager._internal();

  AudioEngineManager._internal();

  late SimpleOpusEncoder simpleOpusEncoder;
  late SimpleOpusDecoder simpleOpusDecoder;

  bool _isInitialized = false;
  Function(Uint8List)? onAudioData;
  Function(double)? onDecibel;

  //🔥 Fixed Opus standard parameters (DO NOT CHANGE)
  static const int sampleRate = 16000;
  static const int channels = 1;
  static const int frameSamples = 320;

  // autoBufferqueue(fixcore)
  final Int16List _pcmBuffer = Int16List(frameSamples * 10);
  int _bufferWriteIndex = 0;

  Future<void> init() async {
    try {
      WidgetsFlutterBinding.ensureInitialized();
      await opus_flutter.load();
      initOpus(await opus_flutter.load());

      // StandardencodeController
      simpleOpusEncoder = SimpleOpusEncoder(
        sampleRate: sampleRate,
        channels: channels,
        application: Application.voip,
      );
      simpleOpusDecoder = SimpleOpusDecoder(
        sampleRate: sampleRate,
        channels: channels,
      );

      _isInitialized = true;
      
      //listenoriginalRecorddata
      // NativeBridge.shared.recordChannel.receiveBroadcastStream().listen((data) {
      //   if (data is Uint8List) _processPcm(data);
      // });
    } catch (e) {
            rethrow;
    }
  }

  //======================== 🔥 Core fix: Auto frame assembly ========================
  void _processPcm(Uint8List pcm8) {
    try {
      final pcm16 = Int16List.view(pcm8.buffer);

      //writeBuffer
      for (int sample in pcm16) {
        if (_bufferWriteIndex < _pcmBuffer.length) {
          _pcmBuffer[_bufferWriteIndex++] = sample;
        }

        //Accumulate 320 samples → encode one frame
        if (_bufferWriteIndex == frameSamples) {
          final frame = _pcmBuffer.sublist(0, frameSamples);
          final opusData = simpleOpusEncoder.encode(input: frame);

          //output
          if (onAudioData != null) onAudioData!(opusData);
          if (onDecibel != null) onDecibel!(_getDecibel(frame));

          //resetBuffer
          _bufferWriteIndex = 0;
        }
      }
    } catch (e) {
          }
  }

  //decibelcalculate
  double _getDecibel(Int16List pcm) {
    num sum = 0;
    for (int s in pcm) {
      sum += s * s;
    }
    final rms = sqrt(sum / pcm.length);
    final db = 20 * log(rms / 32768.0) / ln10;
    return db.isFinite ? db : -60;
  }

  //======================== play ========================
  Future<void> playOpus(Uint8List opusData) async {
    try {
      if (!_isInitialized) return;
      final pcm16 = simpleOpusDecoder.decode(input: opusData);
      final byteData = ByteData(pcm16.length * 2);
      for (int i = 0; i < pcm16.length; i++) {
        byteData.setInt16(i * 2, pcm16[i], Endian.little);
      }
      NativeBridge.shared.sendAudioStream(byteData);
    } catch (e) {
          }
  }

  Future<void> stopPlayOpus() async {
    NativeBridge.shared.sendMessage(.stopPlayPCM);
  }

  //====================== startRecord ======================
  Future<bool> startRecording() async {
    if (!_isInitialized) return false;

    //requestMicrophonepermission
    final perm = await Permission.microphone.request();
    if (!perm.isGranted) {
            return false;
    }

        NativeBridge.shared.sendMessage(.startRecording);
    return true;
  }

  //====================== stopRecord ======================
  Future<void> stopRecording() async {
    NativeBridge.shared.sendMessage(.stopRecording);
      }

  Future<void> dispose() async {
    if (_isInitialized) {
      simpleOpusEncoder.destroy();
      simpleOpusDecoder.destroy();
      _isInitialized = false;
    }
      }
}

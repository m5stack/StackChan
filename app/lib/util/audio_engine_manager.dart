/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:math';
import 'dart:typed_data';

import 'package:cupertino_ui/cupertino_ui.dart';
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
  bool _isRecording = false;
  Future<void>? _initFuture;
  StreamSubscription<dynamic>? _recordSubscription;
  Function(Uint8List)? onAudioData;
  Function(double)? onDecibel;

  // Fixed Opus parameters required by the audio pipeline. Do not change.
  static const int sampleRate = 16000;
  static const int channels = 1;
  static const int frameSamples = 320;

  // Buffer for assembling PCM samples into fixed-size frames.
  final Int16List _pcmBuffer = Int16List(frameSamples * 10);
  int _bufferWriteIndex = 0;
  int _encodedPacketCount = 0;
  int _decodedPacketCount = 0;

  Future<void> init() {
    if (_isInitialized) return Future<void>.value();
    return _initFuture ??= _initialize();
  }

  Future<void> _initialize() async {
    try {
      WidgetsFlutterBinding.ensureInitialized();
      await opus_flutter.load();
      initOpus(await opus_flutter.load());

      // Opus codec configured for 16 kHz mono VoIP audio.
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
      debugPrint(" Audio引擎InitializeSuccess");

      await _recordSubscription?.cancel();
      _recordSubscription = NativeBridge.shared.recordChannel
          .receiveBroadcastStream()
          .listen(
            (data) {
              if (data is Uint8List && _isRecording) _processPcm(data);
            },
            onError: (Object error) {
              debugPrint("AudioRecordStreamFailed: $error");
            },
          );
    } catch (e) {
      _initFuture = null;
      debugPrint(" InitializeFailed: $e");
      rethrow;
    }
  }

  // ==================== PCM frame assembly ====================
  // ignore: unused_element
  void _processPcm(Uint8List pcm8) {
    if (pcm8.lengthInBytes < 2) return;
    try {
      final pcm16 = pcm8.buffer.asInt16List(
        pcm8.offsetInBytes,
        pcm8.lengthInBytes ~/ 2,
      );

      // Append incoming samples to the frame buffer.
      for (int sample in pcm16) {
        if (_bufferWriteIndex < _pcmBuffer.length) {
          _pcmBuffer[_bufferWriteIndex++] = sample;
        }

        // Encode each complete 320-sample frame.
        if (_bufferWriteIndex == frameSamples) {
          final frame = _pcmBuffer.sublist(0, frameSamples);
          final opusData = simpleOpusEncoder.encode(input: frame);
          _encodedPacketCount++;
          if (_encodedPacketCount % 25 == 0) {
            debugPrint(
              "AudioTx frames=$_encodedPacketCount bytes=${opusData.length}",
            );
          }

          // Emit the encoded frame and its input level.
          if (onAudioData != null) onAudioData!(opusData);
          if (onDecibel != null) onDecibel!(_getDecibel(frame));

          // Start buffering the next frame.
          _bufferWriteIndex = 0;
        }
      }
    } catch (e) {
      debugPrint(" EncodeFailed: $e");
    }
  }

  // Calculate the root-mean-square level in dBFS.
  double _getDecibel(Int16List pcm) {
    num sum = 0;
    for (int s in pcm) {
      sum += s * s;
    }
    final rms = sqrt(sum / pcm.length);
    final db = 20 * log(rms / 32768.0) / ln10;
    return db.isFinite ? db : -60;
  }

  // ==================== Playback ====================
  Future<void> playOpus(Uint8List opusData) async {
    try {
      if (!_isInitialized) return;
      final pcm16 = simpleOpusDecoder.decode(input: opusData);
      _decodedPacketCount++;
      if (_decodedPacketCount % 25 == 0) {
        debugPrint("AudioRx frames=$_decodedPacketCount pcm=${pcm16.length}");
      }
      final byteData = ByteData(pcm16.length * 2);
      for (int i = 0; i < pcm16.length; i++) {
        byteData.setInt16(i * 2, pcm16[i], Endian.little);
      }
      NativeBridge.shared.sendAudioStream(byteData);
    } catch (e) {
      debugPrint(" PlayFailed: $e");
    }
  }

  Future<void> stopPlayOpus() async {
    NativeBridge.shared.sendMessage(.stopPlayPCM);
  }

  // ==================== Recording ====================
  Future<bool> startRecording() async {
    if (!_isInitialized) {
      try {
        await init();
      } catch (_) {
        return false;
      }
    }

    // Request microphone access.
    final perm = await Permission.microphone.request();
    if (!perm.isGranted) {
      debugPrint("MicrophonePermissionNot授权");
      return false;
    }

    debugPrint("🎙️ RecordStart (实WhenOpusEncode Int16)");
    _bufferWriteIndex = 0;
    final started = await NativeBridge.shared.sendMessage(.startRecording);
    if (started != true) {
      _isRecording = false;
      return false;
    }
    _isRecording = true;
    return true;
  }

  // ==================== Stop recording ====================
  Future<void> stopRecording() async {
    _isRecording = false;
    _bufferWriteIndex = 0;
    NativeBridge.shared.sendMessage(.stopRecording);
    debugPrint("🛑 RecordStop");
  }

  Future<void> dispose() async {
    if (_isInitialized) {
      simpleOpusEncoder.destroy();
      simpleOpusDecoder.destroy();
      _isInitialized = false;
    }
    debugPrint("♻️ Asset / Resource源ReleaseComplete / Done");
  }
}

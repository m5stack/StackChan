/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

// ignore_for_file: experimental_member_use

import 'dart:async';
import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:ffmpeg_kit_flutter_new/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new/return_code.dart';
import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:just_audio/just_audio.dart';
import 'package:music_feature_analyzer/music_feature_analyzer.dart';
import 'package:path/path.dart' as path;
import 'package:path_provider/path_provider.dart';

class MusicInfo {
  int duration; // In seconds
  String filePath;
  String? title;
  String? artist;
  String? album;
  String? artwork;
  String? lyrics;

  MusicInfo(
    this.duration,
    this.filePath, {
    this.title,
    this.artist,
    this.album,
    this.artwork,
    this.lyrics,
  });

  /// Load the file contents as bytes
  Future<Uint8List> loadData() async {
    try {
      final file = File(filePath);
      if (!await file.exists()) {
        throw FileSystemException("音乐文件不存在", file.path);
      }
      return await file.readAsBytes();
    } catch (e) {
      throw Exception("加载音乐文件失败: $e");
    }
  }

  String get mimeType {
    final ext = path.extension(filePath).toLowerCase();
    switch (ext) {
      case '.wav':
        return 'audio/wav';
      case '.aac':
        return 'audio/aac';
      case '.m4a':
      case '.m4b':
      case '.m4p':
        return 'audio/mp4';
      case '.flac':
        return 'audio/flac';
      case '.ogg':
        return 'audio/ogg';
      case '.aiff':
      case '.aif':
        return 'audio/aiff';
      case '.opus':
        return 'audio/opus';
      case '.alac':
        return 'audio/alac';
      case '.mp3':
      default:
        return 'audio/mpeg';
    }
  }

  Future<List<double>> getProgressData({int targetSampleCount = 100}) async {
    if (targetSampleCount <= 0) {
      throw ArgumentError("目标采样点数必须大于0: $targetSampleCount");
    }
    final audioFile = File(filePath);
    if (!await audioFile.exists()) {
      throw FileSystemException("音频文件不存在", filePath);
    }

    final tempDir = await getTemporaryDirectory();
    final pcmFileName =
        "audio_waveform_${DateTime.now().microsecondsSinceEpoch}.pcm";
    final pcmFilePath = "${tempDir.path}/$pcmFileName";

    try {
      final command =
          '-loglevel error -hide_banner -i "$filePath" -f s16le -ac 1 -ar 16000 -vn "$pcmFilePath"';
      final session = await FFmpegKit.execute(command);
      final returnCode = await session.getReturnCode();
      if (!ReturnCode.isSuccess(returnCode)) {
        final error = await session.getAllLogsAsString();
        throw Exception("FFmpeg转换失败: 码=$returnCode, 错误=$error");
      }

      // Process the PCM file and compute per-chunk volume (RMS)
      final volumeData = await _processPcmFileForVolume(
        pcmFilePath,
        targetSampleCount,
      );
      return volumeData;
    } catch (e) {
      throw Exception("获取音频波动数据失败: $e");
    } finally {
      try {
        final pcmFile = File(pcmFilePath);
        if (await pcmFile.exists()) {
          await pcmFile.delete();
        }
      } catch (e) {
        // Log deletion failures only; do not interrupt the main flow
        debugPrint("DeleteWhenWhenPCMFileFailed: $e");
      }
    }
  }

  /// Process a PCM file and compute per-chunk volume (RMS)
  Future<List<double>> _processPcmFileForVolume(
    String pcmPath,
    int targetSampleCount,
  ) async {
    final file = File(pcmPath);
    final bytes = await file.readAsBytes();
    const sampleSize = 2; //16-bit PCM: 2 bytes per sample
    final totalSamples = bytes.length ~/ sampleSize;

    // No samples: return all zeros
    if (totalSamples == 0) {
      return List.filled(targetSampleCount, 0.0);
    }

    final byteData = ByteData.view(bytes.buffer);
    final volumeValues = <double>[];

    // Number of samples per block
    final samplesPerBlock = (totalSamples / targetSampleCount).ceil();

    // Calculate volume in chunks (RMS)
    for (int blockIndex = 0; blockIndex < targetSampleCount; blockIndex++) {
      // Sample range of the current block
      final startSample = blockIndex * samplesPerBlock;
      final endSample = ((blockIndex + 1) * samplesPerBlock).clamp(
        0,
        totalSamples,
      );
      final blockSampleCount = endSample - startSample;

      // Empty chunk: volume is 0
      if (blockSampleCount <= 0) {
        volumeValues.add(0.0);
        continue;
      }

      //RMS of the current chunk: average volume over this period
      double sumOfSquares = 0.0;
      for (int i = startSample; i < endSample; i++) {
        // Read a 16-bit little-endian PCM sample (-32768 to 32767)
        final int16Value = byteData.getInt16(i * sampleSize, Endian.little);
        // Accumulate the square of the sample
        sumOfSquares += (int16Value * int16Value).toDouble();
      }

      //RMS = sqrt(sum of squares / sample count)
      final rms = sqrt(sumOfSquares / blockSampleCount);
      // Normalize to 0..1 (32767 is the largest 16-bit signed value)
      final normalizedRms = (rms / 32767.0).clamp(0.0, 1.0);

      // Optional: convert to decibels (closer to human perception, range 0..1)
      // Decibel formula: 20 * log10(RMS / 32767), but handle 0 to avoid log(0)
      // final db = normalizedRms > 0 ? 20 * log10(normalizedRms) : -100;
      //final normalizedDb = (db + 100) / 100; // Map to 0~1
      // volumeValues.add(normalizedDb.clamp(0.0, 1.0));

      // Use normalized RMS directly: simpler linear volume
      volumeValues.add(normalizedRms);
    }

    return volumeValues;
  }
}

/// Custom byte-stream audio source for just_audio
class BytesAudioSource extends StreamAudioSource {
  final Uint8List bytes;
  final String contentType;
  final String? id;

  BytesAudioSource(this.bytes, {this.contentType = 'audio/mpeg', this.id});

  @override
  Future<StreamAudioResponse> request([int? start, int? end]) async {
    start ??= 0;
    end ??= bytes.length;
    return StreamAudioResponse(
      sourceLength: bytes.length,
      contentLength: end - start,
      offset: start,
      stream: Stream.value(bytes.sublist(start, end)),
      contentType: contentType,
    );
  }
}

class MusicUtil {
  // Private constructor for the singleton
  MusicUtil._internal() {
    _initAnalyzer();
    _setupPlayerListener(); // Attach the listener early so no events are missed
  }

  static final MusicUtil shared = MusicUtil._internal();

  // Core just_audio player instance
  final AudioPlayer _audioPlayer = AudioPlayer();

  // Callback invoked when playback finishes
  void Function()? _playbackCompletion;

  // Total track duration in seconds
  double _musicDuration = 0.0;

  // Current playback position in seconds
  double _currentPosition = 0.0;

  // Info for the track currently loaded
  MusicInfo? _currentMusicInfo;

  /// Initialize the music feature analyzer
  Future<void> _initAnalyzer() async {
    try {
      await MusicFeatureAnalyzer.initialize();
      debugPrint("MusicFeatureAnalyzer InitializeSuccess");
    } catch (e) {
      debugPrint("MusicFeatureAnalyzer InitializeFailed: $e");
    }
  }

  /// Configure the player state and stream listeners
  void _setupPlayerListener() {
    _audioPlayer.setVolume(1.0);

    // Listen to player state (playing + processing state)
    _audioPlayer.playerStateStream.listen((PlayerState state) {
      debugPrint(
        "PlayerState变化: Play=${state.playing}, HandleState=${state.processingState}",
      );

      // Handle playback completion
      if (state.processingState == ProcessingState.completed) {
        debugPrint(
          "MusicPlayComplete / Done,LoopState: ${_audioPlayer.loopMode}",
        );
        _currentPosition = 0.0; // Reset progress

        // Replay when looping one track; otherwise fire the completion callback
        if (_audioPlayer.loopMode == LoopMode.one &&
            _currentMusicInfo != null) {
          _audioPlayer.seek(Duration.zero);
          _audioPlayer.play();
        } else {
          _playbackCompletion?.call();
          _playbackCompletion = null;
        }
      }

      // Reset progress when the player becomes idle
      if (state.processingState == ProcessingState.idle) {
        _currentPosition = 0.0;
      }
    });

    // Listen for duration changes
    _audioPlayer.durationStream.listen((Duration? duration) {
      if (duration != null) {
        _musicDuration = duration.inMilliseconds / 1000.0;
        debugPrint("MusicDurationUpdate: $_musicDuration Second(s)");
      }
    });

    // Listen for position updates
    _audioPlayer.positionStream.listen((Duration position) {
      _currentPosition = position.inMilliseconds / 1000.0;
      // Keep progress from exceeding the total duration
      if (_currentPosition > _musicDuration && _musicDuration > 0) {
        _currentPosition = _musicDuration;
      }
    });

    // Listen for player errors
    _audioPlayer.errorStream.listen((PlayerException? e) {
      if (e != null) {
        debugPrint("PlayerError: 代码=${e.code}, Message=${e.message}");
      }
    });
  }

  /// Play audio from an in-memory byte buffer
  Future<void> playMusicData(
    Uint8List data, {
    String contentType = 'audio/mpeg',
  }) async {
    try {
      await stopMusic();
      _musicDuration = 0.0;
      _currentPosition = 0.0;

      // Load through the custom byte-stream source

      final audioSource = BytesAudioSource(data, contentType: contentType);
      await _audioPlayer.setAudioSource(audioSource);
      await _audioPlayer.play();

      debugPrint("MusicByteDataPlaySuccess,VolumeAlreadySettingsIs1.0");
    } on PlayerException catch (e) {
      debugPrint("PlayMusicByteDataFailed: 代码=${e.code}, Message=${e.message}");
      throw Exception("播放失败: ${e.message}");
    } catch (e) {
      debugPrint("PlayMusicByteDataFailed: $e");
      throw Exception("播放失败: $e");
    }
  }

  /// Play one track and run [completion] when it ends
  Future<void> playMusicOnce(MusicInfo musicInfo, Function() completion) async {
    _playbackCompletion = completion;
    await playMusic(musicInfo, isLoop: false); // Single playback: force looping off
  }

  /// Play a URL once; calling it again stops playback and starts over
  Future<void> playUrlMusicOnce(String? url, {Function()? completion}) async {
    if (url == null) {
      debugPrint("PlayFailed:URLIs null/empty");
      return;
    }
    try {
      // Stop any current playback first
      await stopMusic();

      // Store the completion callback
      _playbackCompletion = completion;

      // Disable looping
      _audioPlayer.setLoopMode(LoopMode.off);

      // Load the URL directly with setUrl
      await _audioPlayer.setUrl(url);
      await _audioPlayer.play();

      debugPrint("OnlineMusicPlaySuccess:$url");
    } on PlayerException catch (e) {
      debugPrint("PlayOnlineMusicFailed: 代码=${e.code}, Message=${e.message}");
      throw Exception("播放失败: ${e.message}");
    } catch (e) {
      debugPrint("PlayOnlineMusicFailed: $e");
      throw Exception("播放失败: $e");
    }
  }

  /// Core playback method; supports looping
  Future<void> playMusic(MusicInfo? musicInfo, {bool isLoop = false}) async {
    if (musicInfo == null) {
      debugPrint("PlayFailed:MusicInfoIs null/empty");
      return;
    }

    // Remember the current track
    _currentMusicInfo = musicInfo;

    // Apply the loop mode
    _audioPlayer.setLoopMode(isLoop ? LoopMode.one : LoopMode.off);

    try {
      final data = await musicInfo.loadData();
      final contentType = musicInfo.mimeType;
      await playMusicData(data, contentType: contentType);
      debugPrint("PlayMusicSuccess:${musicInfo.title ?? musicInfo.filePath}");
    } on PlayerException catch (e) {
      debugPrint("PlayMusicFailed: 代码=${e.code}, Message=${e.message}");
      throw Exception("播放失败: ${e.message}");
    } catch (e) {
      debugPrint("PlayMusicFailed: $e");
      throw Exception("播放失败: $e");
    }
  }

  /// Stop playback and reset progress
  Future<void> stopMusic() async {
    await _audioPlayer.stop();
    await _audioPlayer.seek(Duration.zero); // Reset progress to the beginning

    _currentPosition = 0.0;
    _playbackCompletion = null;
    _currentMusicInfo = null;
    debugPrint("MusicAlreadyStopPlay");
  }

  /// Pause playback
  Future<void> pauseMusic() async {
    if (_audioPlayer.playing) {
      await _audioPlayer.pause();
      debugPrint("MusicAlreadyPause");
    }
  }

  /// Resume playback
  Future<void> resumeMusic() async {
    if (!_audioPlayer.playing && _currentMusicInfo != null) {
      await _audioPlayer.play();
      debugPrint("MusicAlreadyResumePlay");
    }
  }

  /// Enable or disable single-track looping
  void setMusicLoop(bool isLoop) {
    final loopMode = isLoop ? LoopMode.one : LoopMode.off;
    _audioPlayer.setLoopMode(loopMode);
    debugPrint("LoopPlayStateAlreadySettingsIs: $isLoop (LoopMode: $loopMode)");
  }

  /// Seek to [seconds] within the current track
  Future<void> seekTo(double seconds) async {
    if (seconds < 0 || seconds > _musicDuration) {
      debugPrint(
        "ProgressJumpFailed:No效ProgressValue $seconds,TotalDuration $_musicDuration",
      );
      return;
    }
    await _audioPlayer.seek(Duration(seconds: seconds.toInt()));
    _currentPosition = seconds;
    debugPrint("ProgressAlreadyJump到: $seconds Second(s)");
  }

  /// Set the volume; [volume] must be in 0.0..1.0
  Future<void> setVolume(double volume) async {
    if (volume < 0.0 || volume > 1.0) {
      debugPrint(
        "VolumeSettingsFailed:No效Value $volume,Range需In 0.0 ~ 1.0 Between",
      );
      return;
    }
    await _audioPlayer.setVolume(volume);
    debugPrint("VolumeAlreadySettingsIs: $volume");
  }

  /// Set the playback speed
  Future<void> setPlaybackSpeed(double speed) async {
    if (speed <= 0) {
      debugPrint("Play速度SettingsFailed:No效Value $speed,需Greater than 0");
      return;
    }
    await _audioPlayer.setSpeed(speed);
    debugPrint("Play速度AlreadySettingsIs: $speed");
  }

  /// Get the current playback position in seconds
  double getCurrentPosition() => _currentPosition;

  /// Get the total track duration in seconds
  double getMusicDuration() => _musicDuration;

  /// Get whether single-track looping is enabled
  bool getIsLoop() => _audioPlayer.loopMode == LoopMode.one;

  /// Get whether playback is active
  bool isPlaying() => _audioPlayer.playing;

  /// Release player resources; call when the page is disposed
  Future<void> dispose() async {
    await stopMusic();
    await _audioPlayer.dispose();
    _currentMusicInfo = null;
    _playbackCompletion = null;
    debugPrint("MusicUtil Asset / Resource源AlreadyRelease");
  }

  /// Resolve track metadata for [urlString], using the local cache when possible
  Future<MusicInfo?> getMusicInfoAsync(String urlString) async {
    const tag = "MusicUtil/getMusicInfoAsync";
    try {
      debugPrint("$tag StartHandleMusicURL: $urlString");

      //1. Parse the URL
      final uri = Uri.parse(urlString);
      if (!uri.isAbsolute) {
        debugPrint("$tag URLIs not绝ForPath,ParseFailed");
        return null;
      }

      //2. Prepare the cache directory and a stable key
      final requestedExtension = path.extension(uri.path).toLowerCase();
      final cacheKey = uri.hashCode.toRadixString(16);
      final cacheDir = await getApplicationDocumentsDirectory();
      final musicCacheDir = Directory(path.join(cacheDir.path, 'music_cache'));
      if (!await musicCacheDir.exists()) {
        await musicCacheDir.create(recursive: true);
      }

      //3. Reuse a cached file regardless of the original URL extension
      final cachedFile = await _findCachedMusicFile(musicCacheDir, cacheKey);
      if (cachedFile != null) {
        debugPrint("$tag UseCacheFile: ${cachedFile.path}");
        return await _extractMetadataFromFile(cachedFile.path, uri);
      }

      //4. Download to a temporary file, then detect the real format
      final downloadFile = File(
        path.join(musicCacheDir.path, '$cacheKey.download'),
      );
      if (await downloadFile.exists()) {
        await downloadFile.delete();
      }

      await _downloadFile(uri, downloadFile);
      final stat = await downloadFile.stat();
      final fileSizeKB = stat.size / 1024;
      if (fileSizeKB < 10) {
        debugPrint("$tag DownloadFile过小($fileSizeKB KB),No效File");
        await _safeDeleteFile(downloadFile);
        return null;
      }

      final detectedExtension =
          await _detectAudioExtension(downloadFile) ?? requestedExtension;
      if (!_supportedAudioExtensions.contains(detectedExtension)) {
        debugPrint(
          "$tag Not支持DetectedFile格式: requested=$requestedExtension, detected=$detectedExtension",
        );
        await _safeDeleteFile(downloadFile);
        return null;
      }

      final finalFilePath = path.join(
        musicCacheDir.path,
        '$cacheKey$detectedExtension',
      );
      final finalFile = File(finalFilePath);
      if (await finalFile.exists()) {
        await finalFile.delete();
      }
      await downloadFile.rename(finalFilePath);

      debugPrint(
        "$tag RealAudioFormatDetected: requested=$requestedExtension, detected=$detectedExtension, file=$finalFilePath",
      );

      //5. Extract metadata using the detected extension
      return await _extractMetadataFromFile(finalFilePath, uri);
    } catch (e, stackTrace) {
      debugPrint(
        "$tag ExecuteFailed:ExceptionType=${e.runtimeType},Message=$e,堆Stack=$stackTrace",
      );
      return null;
    }
  }

  static const Set<String> _supportedAudioExtensions = {
    '.mp3',
    '.wav',
    '.m4a',
    '.flac',
    '.aac',
    '.ogg',
    '.aiff',
    '.alac',
    '.amr',
    '.m4b',
    '.m4p',
    '.opus',
  };

  Future<File?> _findCachedMusicFile(
    Directory cacheDir,
    String cacheKey,
  ) async {
    for (final extension in _supportedAudioExtensions) {
      final file = File(path.join(cacheDir.path, '$cacheKey$extension'));
      if (!await file.exists()) {
        continue;
      }

      final stat = await file.stat();
      if (stat.size < 10 * 1024) {
        debugPrint(
          "MusicUtil/_findCachedMusicFile CacheFile过小(${stat.size} bytes),Delete:${file.path}",
        );
        await _safeDeleteFile(file);
        continue;
      }

      return file;
    }

    return null;
  }

  Future<String?> _detectAudioExtension(File file) async {
    try {
      final header = await file.openRead(0, 64).first;
      if (header.length < 4) {
        return null;
      }

      if (header.length >= 3 &&
          header[0] == 0x49 &&
          header[1] == 0x44 &&
          header[2] == 0x33) {
        return '.mp3';
      }
      if (header[0] == 0xFF && (header[1] & 0xE0) == 0xE0) {
        return '.mp3';
      }

      if (header.length >= 12 &&
          header[0] == 0x52 &&
          header[1] == 0x49 &&
          header[2] == 0x46 &&
          header[3] == 0x46 &&
          header[8] == 0x57 &&
          header[9] == 0x41 &&
          header[10] == 0x56 &&
          header[11] == 0x45) {
        return '.wav';
      }

      if (header[0] == 0x66 &&
          header[1] == 0x4C &&
          header[2] == 0x61 &&
          header[3] == 0x43) {
        return '.flac';
      }

      if (header[0] == 0x4F &&
          header[1] == 0x67 &&
          header[2] == 0x67 &&
          header[3] == 0x53) {
        return '.ogg';
      }

      if (header.length >= 12 &&
          header[0] == 0x46 &&
          header[1] == 0x4F &&
          header[2] == 0x52 &&
          header[3] == 0x4D &&
          header[8] == 0x41 &&
          header[9] == 0x49 &&
          header[10] == 0x46 &&
          (header[11] == 0x46 || header[11] == 0x43)) {
        return '.aiff';
      }

      if (header.length >= 12 &&
          header[4] == 0x66 &&
          header[5] == 0x74 &&
          header[6] == 0x79 &&
          header[7] == 0x70) {
        return '.m4a';
      }

      return null;
    } catch (e) {
      debugPrint("MusicUtil/_detectAudioExtension Failed: $e");
      return null;
    }
  }

  Future<void> _safeDeleteFile(File file) async {
    try {
      if (await file.exists()) {
        await file.delete();
      }
    } catch (e) {
      debugPrint("MusicUtil/_safeDeleteFile Failed: ${file.path}, error=$e");
    }
  }

  /// Download [uri] into [file]
  Future<void> _downloadFile(Uri uri, File file) async {
    const tag = "MusicUtil/_downloadFile";
    final httpClient = HttpClient();
    try {
      final request = await httpClient.getUrl(uri);
      final response = await request.close();

      if (response.statusCode != HttpStatus.ok) {
        throw Exception("下载失败：状态码 ${response.statusCode}，URL=$uri");
      }

      await response.pipe(file.openWrite());
      debugPrint("$tag FileDownloadComplete / Done:${file.path}");
    } catch (e) {
      debugPrint("$tag DownloadFailed:$e");
      rethrow;
    } finally {
      httpClient.close();
    }
  }

  /// Extract track metadata from a local file
  Future<MusicInfo?> _extractMetadataFromFile(String filePath, Uri uri) async {
    const tag = "MusicUtil/_extractMetadataFromFile";
    try {
      final song = await MusicFeatureAnalyzer.metadata(filePath);
      if (song == null) {
        debugPrint("$tag No法ExtractMetadata:MusicFeatureAnalyzerReturnnull");
        return null;
      }

      final durationSec = song.duration ~/ 1000; // Convert to seconds
      debugPrint(
        "$tag 提取元数据成功：时长=$durationSec秒，标题=${song.title}，艺术家=${song.artist}",
      );

      return MusicInfo(
        durationSec,
        filePath,
        title: song.title,
        artist: song.artist,
        album: song.album,
        artwork: song.albumArt,
      );
    } catch (e, stackTrace) {
      debugPrint("$tag ExtractMetadataFailed:Exception=$e,堆Stack=$stackTrace");
      return null;
    }
  }

  /// Delete cached music files older than [maxAge]
  Future<void> clearExpiredCache({
    Duration maxAge = const Duration(days: 7),
  }) async {
    const tag = "MusicUtil/clearExpiredCache";
    try {
      final cacheDir = await getTemporaryDirectory();
      final files = await cacheDir.list().toList();
      final now = DateTime.now();
      int deletedCount = 0;

      for (final file in files) {
        if (file is File) {
          final extension = path.extension(file.path).toLowerCase();
          if (['.mp3', '.wav', '.m4a', '.flac'].contains(extension)) {
            final stat = await file.stat();
            final fileAge = now.difference(stat.modified);
            if (fileAge > maxAge) {
              await file.delete();
              deletedCount++;
              debugPrint(
                "$tag DeleteExpiredCache:${file.path},ExistsDuration=${fileAge.inDays}天",
              );
            }
          }
        }
      }
      debugPrint("$tag CacheCleanupComplete / Done,共Delete$deletedCount个File");
    } catch (e) {
      debugPrint("$tag CacheCleanupFailed:$e");
    }
  }
}

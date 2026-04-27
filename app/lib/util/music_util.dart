/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:ffmpeg_kit_flutter_new/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new/return_code.dart';
import 'package:flutter/cupertino.dart';
import 'package:just_audio/just_audio.dart';
import 'package:music_feature_analyzer/music_feature_analyzer.dart';
import 'package:path/path.dart' as path;
import 'package:path_provider/path_provider.dart';

class MusicInfo {
  int duration; //单位：秒
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

  ///loadmusicfileBytedata
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
      case '.m4a':
        return 'audio/mp4';
      case '.flac':
        return 'audio/flac';
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

      //Process PCM file, calculate chunk volume (RMS)
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
        //onlyPrintdeletefaillog,NotinterruptMainStreamProcess / Thread
        debugPrint("DeleteWhenWhenPCMFileFailed: $e");
      }
    }
  }

  ///Process PCM file, calculate chunk volume (RMS/decibel)
  Future<List<double>> _processPcmFileForVolume(
    String pcmPath,
    int targetSampleCount,
  ) async {
    final file = File(pcmPath);
    final bytes = await file.readAsBytes();
    const sampleSize = 2; //16-bit PCM = 2 bytes/sample
    final totalSamples = bytes.length ~/ sampleSize;

    //Boundary: return all zeros when no samples
    if (totalSamples == 0) {
      return List.filled(targetSampleCount, 0.0);
    }

    final byteData = ByteData.view(bytes.buffer);
    final volumeValues = <double>[];

    //calculateeachBlockShouldContainssampleCount / Number
    final samplesPerBlock = (totalSamples / targetSampleCount).ceil();

    //Calculate volume in chunks (RMS)
    for (int blockIndex = 0; blockIndex < targetSampleCount; blockIndex++) {
      //calculatecurrentBlocksamplerange
      final startSample = blockIndex * samplesPerBlock;
      final endSample = ((blockIndex + 1) * samplesPerBlock).clamp(
        0,
        totalSamples,
      );
      final blockSampleCount = endSample - startSample;

      //Boundary: chunk with no samples, volume is 0
      if (blockSampleCount <= 0) {
        volumeValues.add(0.0);
        continue;
      }

      //Calculate RMS of current chunk: reflects average volume in this period
      double sumOfSquares = 0.0;
      for (int i = startSample; i < endSample; i++) {
        //Read 16-bit little-endian PCM sample (range: -32768 ~ 32767)
        final int16Value = byteData.getInt16(i * sampleSize, Endian.little);
        //calculateSquareand
        sumOfSquares += (int16Value * int16Value).toDouble();
      }

      //RMS = sqrt(sum of squares / sample count)
      final rms = sqrt(sumOfSquares / blockSampleCount);
      //Normalize to 0~1 range (32767 is max value for 16-bit signed integer)
      final normalizedRms = (rms / 32767.0).clamp(0.0, 1.0);

      //Optional: Convert to decibels (dB) (closer to human perception, range: 0~1)
      //Decibel formula: 20 * log10(RMS / 32767), but handle 0 to avoid log(0)
      // final db = normalizedRms > 0 ? 20 * log10(normalizedRms) : -100;
      //final normalizedDb = (db + 100) / 100; // Map to 0~1
      // volumeValues.add(normalizedDb.clamp(0.0, 1.0));

      //Use normalized RMS directly (simpler, linear volume representation)
      volumeValues.add(normalizedRms);
    }

    return volumeValues;
  }
}

///Custom byte stream audio source (adapt just_audio)
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
  //Singletonmode
  MusicUtil._internal() {
    _initAnalyzer();
    _setupPlayerListener(); //提beforeinitlistener，avoid遗漏
  }

  static final MusicUtil shared = MusicUtil._internal();

  //Core player instance (just_audio)
  final AudioPlayer _audioPlayer = AudioPlayer();

  //playcompletecallback
  void Function()? _playbackCompletion;

  //musicduration(Second(s))
  double _musicDuration = 0.0;

  //currentplayprogress(Second(s))
  double _currentPosition = 0.0;

  //currentplaymusicinfo
  MusicInfo? _currentMusicInfo;

  ///initmusicAnalyzer
  Future<void> _initAnalyzer() async {
    try {
      await MusicFeatureAnalyzer.initialize();
      debugPrint("MusicFeatureAnalyzer InitializeSuccess");
    } catch (e) {
      debugPrint("MusicFeatureAnalyzer InitializeFailed: $e");
    }
  }

  ///configplayerlistener(System1Managerstate)
  void _setupPlayerListener() {
    _audioPlayer.setVolume(1.0);

    //playerstatelisten(Containsplaystateandhandlestate)
    _audioPlayer.playerStateStream.listen((PlayerState state) {
      debugPrint("PlayerState变化: Play=${state.playing}, HandleState=${state.processingState}");

      //Playback completion check (handle completed status)
      if (state.processingState == ProcessingState.completed) {
        debugPrint("MusicPlayComplete / Done,LoopState: ${_audioPlayer.loopMode}");
        _currentPosition = 0.0; //resetprogress

        //SingleloopThenreplay,elseexecutecompletecallback
        if (_audioPlayer.loopMode == LoopMode.one &&
            _currentMusicInfo != null) {
          _audioPlayer.seek(Duration.zero);
          _audioPlayer.play();
        } else {
          _playbackCompletion?.call();
          _playbackCompletion = null;
        }
      }

      //stopstateresetprogress
      if (state.processingState == ProcessingState.idle) {
        _currentPosition = 0.0;
      }
    });

    //durationChangelisten
    _audioPlayer.durationStream.listen((Duration? duration) {
      if (duration != null) {
        _musicDuration = duration.inMilliseconds / 1000.0;
        debugPrint("MusicDurationUpdate: $_musicDuration Second(s)");
      }
    });

    //playprogresslisten
    _audioPlayer.positionStream.listen((Duration position) {
      _currentPosition = position.inMilliseconds / 1000.0;
      //preventprogressExceeds totalduration
      if (_currentPosition > _musicDuration && _musicDuration > 0) {
        _currentPosition = _musicDuration;
      }
    });

    //errorlisten
    _audioPlayer.errorStream.listen((PlayerException? e) {
      if (e != null) {
        debugPrint("PlayerError: 代码=${e.code}, Message=${e.message}");
      }
    });
  }

  ///playBytedataFormat / Formmusic
  Future<void> playMusicData(
    Uint8List data, {
    String contentType = 'audio/mpeg',
  }) async {
    try {
      await stopMusic();
      _musicDuration = 0.0;
      _currentPosition = 0.0;

      //usecustomByteStreamaudioSourceload

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

  ///playSinglemusic(playcompleteafterexecutecallback)
  Future<void> playMusicOnce(MusicInfo musicInfo, Function() completion) async {
    _playbackCompletion = completion;
    await playMusic(musicInfo, isLoop: false); //单次play强制closeloop
  }

  ///PlayOnlineMusic1Time(s),RepeatCallThenStopFrontFrom beginningPlay
  Future<void> playUrlMusicOnce(String? url, {Function()? completion}) async {
    if (url == null) {
      debugPrint("PlayFailed:URLIs null/empty");
      return;
    }
    try {
      // First / PreviouslyStopFrontPlay
      await stopMusic();

      // SetComplete / DoneCallback
      _playbackCompletion = completion;

      // SetIsNotLoop
      _audioPlayer.setLoopMode(LoopMode.off);

      // DirectlyUse setUrl Load URL
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

  ///coreplaymethod（supportloop）
  Future<void> playMusic(MusicInfo? musicInfo, {bool isLoop = false}) async {
    if (musicInfo == null) {
      debugPrint("PlayFailed:MusicInfoIs null/empty");
      return;
    }

    //Recordcurrentplaymusicinfo
    _currentMusicInfo = musicInfo;

    //Set loop mode (just_audio LoopMode)
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

  ///stopplay
  Future<void> stopMusic() async {
    await _audioPlayer.stop();
    await _audioPlayer.seek(Duration.zero); //resetprogressto开头

    _currentPosition = 0.0;
    _playbackCompletion = null;
    _currentMusicInfo = null;
    debugPrint("MusicAlreadyStopPlay");
  }

  ///pauseplay
  Future<void> pauseMusic() async {
    if (_audioPlayer.playing) {
      await _audioPlayer.pause();
      debugPrint("MusicAlreadyPause");
    }
  }

  ///resumeplay
  Future<void> resumeMusic() async {
    if (!_audioPlayer.playing && _currentMusicInfo != null) {
      await _audioPlayer.play();
      debugPrint("MusicAlreadyResumePlay");
    }
  }

  ///setloopplaystate
  void setMusicLoop(bool isLoop) {
    final loopMode = isLoop ? LoopMode.one : LoopMode.off;
    _audioPlayer.setLoopMode(loopMode);
    debugPrint("LoopPlayStateAlreadySettingsIs: $isLoop (LoopMode: $loopMode)");
  }

  ///jumpplayprogress
  Future<void> seekTo(double seconds) async {
    if (seconds < 0 || seconds > _musicDuration) {
      debugPrint("ProgressJumpFailed:No效ProgressValue $seconds,TotalDuration $_musicDuration");
      return;
    }
    await _audioPlayer.seek(Duration(seconds: seconds.toInt()));
    _currentPosition = seconds;
    debugPrint("ProgressAlreadyJump到: $seconds Second(s)");
  }

  ///Set volume (0.0 ~ 1.0)
  Future<void> setVolume(double volume) async {
    if (volume < 0.0 || volume > 1.0) {
      debugPrint("VolumeSettingsFailed:No效Value $volume,Range需In 0.0 ~ 1.0 Between");
      return;
    }
    await _audioPlayer.setVolume(volume);
    debugPrint("VolumeAlreadySettingsIs: $volume");
  }

  ///setplayspeed
  Future<void> setPlaybackSpeed(double speed) async {
    if (speed <= 0) {
      debugPrint("Play速度SettingsFailed:No效Value $speed,需Greater than 0");
      return;
    }
    await _audioPlayer.setSpeed(speed);
    debugPrint("Play速度AlreadySettingsIs: $speed");
  }

  ///Getcurrentplayprogress(Second(s))
  double getCurrentPosition() => _currentPosition;

  ///GetmusicTotalduration(Second(s))
  double getMusicDuration() => _musicDuration;

  ///Getcurrentloopstate
  bool getIsLoop() => _audioPlayer.loopMode == LoopMode.one;

  ///GetcurrentplayerwhetherCurrentlyinplay
  bool isPlaying() => _audioPlayer.playing;

  ///releaseplayerAsset / ResourceSource(pagedisposewhenCall)
  Future<void> dispose() async {
    await stopMusic();
    await _audioPlayer.dispose();
    _currentMusicInfo = null;
    _playbackCompletion = null;
    debugPrint("MusicUtil Asset / Resource源AlreadyRelease");
  }

  ///improveaftermusicinfoparse(With / CarryVerboselog+cacheverify)
  Future<MusicInfo?> getMusicInfoAsync(String urlString) async {
    const tag = "MusicUtil/getMusicInfoAsync";
    try {
      debugPrint("$tag StartHandleMusicURL: $urlString");

      //1. Parse URL
      final uri = Uri.parse(urlString);
      if (!uri.isAbsolute) {
        debugPrint("$tag URLIs not绝ForPath,ParseFailed");
        return null;
      }

      //2. Generate cache file info
      final extension = path.extension(uri.path);
      if (extension.isEmpty ||
          ![
            '.mp3',
            '.wav',
            '.m4a',
            '.flac',
          ].contains(extension.toLowerCase())) {
        debugPrint("$tag Not支持File格式: $extension");
        return null;
      }
      final fileName = '${uri.hashCode.toRadixString(16)}$extension';
      //useDocumentDirectoryAnd / WhileNotisWhenwhenDirectory,avoidSystemautocleancachefile
      final cacheDir = await getApplicationDocumentsDirectory();
      final musicCacheDir = Directory(path.join(cacheDir.path, 'music_cache'));
      if (!await musicCacheDir.exists()) {
        await musicCacheDir.create(recursive: true);
      }
      final filePath = path.join(musicCacheDir.path, fileName);

      //3. Check cache file
      final file = File(filePath);
      if (await file.exists()) {
        final stat = await file.stat();
        final fileSizeKB = stat.size / 1024;
        if (fileSizeKB < 10) {
          debugPrint("$tag CacheFile过小($fileSizeKB KB),DeleteAndAgainDownload");
          await file.delete();
        } else {
          debugPrint("$tag UseCacheFile: $filePath");
          return await _extractMetadataFromFile(filePath, uri);
        }
      }

      //4. Download file
      await _downloadFile(uri, file);
      final stat = await file.stat();
      final fileSizeKB = stat.size / 1024;
      if (fileSizeKB < 10) {
        debugPrint("$tag DownloadFile过小($fileSizeKB KB),No效File");
        return null;
      }

      //5. Extract metadata
      return await _extractMetadataFromFile(filePath, uri);
    } catch (e, stackTrace) {
      debugPrint("$tag ExecuteFailed:ExceptionType=${e.runtimeType},Message=$e,堆Stack=$stackTrace");
      return null;
    }
  }

  ///DownLoadfiletoLocalcache
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

  ///fromfileExtractmusicMetadata / Metadata
  Future<MusicInfo?> _extractMetadataFromFile(String filePath, Uri uri) async {
    const tag = "MusicUtil/_extractMetadataFromFile";
    try {
      final song = await MusicFeatureAnalyzer.metadata(filePath);
      if (song == null) {
        debugPrint("$tag No法ExtractMetadata:MusicFeatureAnalyzerReturnnull");
        return null;
      }

      final durationSec = song.duration ~/ 1000; //convertas秒
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

  ///cleanExpiredmusiccache(optional:Periodicallyclean)
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
              debugPrint("$tag DeleteExpiredCache:${file.path},ExistsDuration=${fileAge.inDays}天");
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

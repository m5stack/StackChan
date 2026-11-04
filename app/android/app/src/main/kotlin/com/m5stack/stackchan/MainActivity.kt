/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package com.m5stack.stackchan

import android.Manifest
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.AudioTrack
import androidx.annotation.RequiresPermission
import com.m5stack.stackchan.bluetooth.NativeBluetoothBridge
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.BasicMessageChannel
import io.flutter.plugin.common.BinaryCodec
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicBoolean

class MainActivity : FlutterActivity() {

    private lateinit var channel: MethodChannel
    private lateinit var audioPlayChannel: BasicMessageChannel<ByteBuffer>
    private lateinit var recordChannel: EventChannel
    private var bluetoothBridge: NativeBluetoothBridge? = null

    private val SAMPLE_RATE = 16000
    private val CHANNEL_CONFIG = AudioFormat.CHANNEL_OUT_MONO
    private val AUDIO_FORMAT = AudioFormat.ENCODING_PCM_16BIT
    private var audioTrack: AudioTrack? = null
    private var audioManager: AudioManager? = null

    private val RECORD_CHANNEL_CONFIG = AudioFormat.CHANNEL_IN_MONO
    private var audioRecord: AudioRecord? = null
    private val isRecording = AtomicBoolean(false)
    private val recordingLock = Any()
    private val isAudioEventPending = AtomicBoolean(false)
    private var recordBufferSize = 0
    @Volatile
    private var eventSink: EventChannel.EventSink? = null


    @RequiresPermission(Manifest.permission.RECORD_AUDIO)
    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        val messenger = flutterEngine.dartExecutor.binaryMessenger
        bluetoothBridge = NativeBluetoothBridge(this, messenger).also { it.setup() }

        // Register the method channel
        channel = MethodChannel(
            messenger,
            "com.m5stack.stackchan/native"
        )
        channel.setMethodCallHandler { call, result ->
            methodCallHandler(call, result)
        }
        audioPlayChannel = BasicMessageChannel(
            messenger, "com.m5stack.stackchan/audio_play",
            BinaryCodec.INSTANCE
        )
        audioPlayChannel.setMessageHandler { buffer, reply ->
            buffer?.let { playAudio(it) }
            reply.reply(null)
        }
        recordChannel = EventChannel(messenger, "com.m5stack.stackchan/record")
        recordChannel.setStreamHandler(object : EventChannel.StreamHandler {
            override fun onListen(
                p0: Any?,
                p1: EventChannel.EventSink?
            ) {
                eventSink = p1
            }

            override fun onCancel(p0: Any?) {
                eventSink = null
                stopRecording()
            }
        })
        audioManager = getSystemService(AUDIO_SERVICE) as AudioManager
        initAudioPlayer()
        initAudioRecorder()
    }

    private fun initAudioPlayer() {
        val bufferSize = AudioTrack.getMinBufferSize(SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_FORMAT)
        audioTrack = AudioTrack(
            AudioManager.STREAM_MUSIC, SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_FORMAT, bufferSize,
            AudioTrack.MODE_STREAM
        )
        audioTrack?.play()
    }

    @RequiresPermission(Manifest.permission.RECORD_AUDIO)
    private fun initAudioRecorder() {
        val bufferSize =
            AudioRecord.getMinBufferSize(SAMPLE_RATE, RECORD_CHANNEL_CONFIG, AUDIO_FORMAT)
        if (bufferSize <= 0) {
            recordBufferSize = 0
            audioRecord = null
            return
        }

        val recorder = AudioRecord(
            android.media.MediaRecorder.AudioSource.MIC,
            SAMPLE_RATE,
            RECORD_CHANNEL_CONFIG,
            AUDIO_FORMAT,
            bufferSize
        )
        if (recorder.state != AudioRecord.STATE_INITIALIZED) {
            recorder.release()
            audioRecord = null
            recordBufferSize = 0
            return
        }

        audioRecord = recorder
        recordBufferSize = bufferSize
    }

    private fun playAudio(buffer: ByteBuffer) {
        val data = ByteArray(buffer.remaining())
        buffer.get(data)
        if (audioTrack?.playState != AudioTrack.PLAYSTATE_PLAYING) {
            audioTrack?.play()
        }
        audioTrack?.write(data, 0, data.size)
    }

    // ====================== Core fix 1: recording lifecycle ======================
    @RequiresPermission(Manifest.permission.RECORD_AUDIO)
    private fun startRecording() {
        val recorder: AudioRecord
        val bufferSize: Int
        synchronized(recordingLock) {
            if (isRecording.get()) return

            if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
                audioRecord?.release()
                audioRecord = null
                initAudioRecorder()
            }

            recorder = audioRecord ?: return
            try {
                recorder.startRecording()
            } catch (_: IllegalStateException) {
                return
            }

            if (recorder.recordingState != AudioRecord.RECORDSTATE_RECORDING) {
                return
            }

            isRecording.set(true)
            bufferSize = recordBufferSize
            audioManager?.let { manager ->
                manager.mode = AudioManager.MODE_IN_COMMUNICATION
                manager.isSpeakerphoneOn = true
            }
        }

        Thread {
            val buffer = ByteArray(bufferSize)
            while (isRecording.get()) {
                val readSize = try {
                    recorder.read(buffer, 0, buffer.size)
                } catch (_: IllegalStateException) {
                    break
                }
                if (readSize > 0) {
                    // EventChannel posts to Flutter's main queue. Keep at most one frame queued
                    // so a slow or absent Dart subscriber cannot exhaust the process heap.
                    if (eventSink == null || !isAudioEventPending.compareAndSet(false, true)) {
                        continue
                    }
                    val data = buffer.copyOf(readSize)
                    runOnUiThread {
                        try {
                            eventSink?.success(data)
                        } finally {
                            isAudioEventPending.set(false)
                        }
                    }
                }
            }
        }.start()
    }

    private fun stopRecording() {
        synchronized(recordingLock) {
            isRecording.set(false)
            audioManager?.let { manager ->
                manager.isSpeakerphoneOn = false
                manager.mode = AudioManager.MODE_NORMAL
            }
            val recorder = audioRecord ?: return
            if (recorder.state != AudioRecord.STATE_INITIALIZED ||
                recorder.recordingState != AudioRecord.RECORDSTATE_RECORDING
            ) {
                return
            }

            try {
                recorder.stop()
            } catch (_: IllegalStateException) {
                // The recorder can become invalid when the audio service is torn down.
            }
        }
    }

    // ====================== Core fix 2: method channel handling ======================
    @RequiresPermission(Manifest.permission.RECORD_AUDIO)
    private fun methodCallHandler(call: MethodCall, result: MethodChannel.Result) {
        when (call.method) {
            "stopPlayPCM" -> {
                if (audioTrack?.playState == AudioTrack.PLAYSTATE_PLAYING) {
                    audioTrack?.pause()
                }
                result.success(null) // Always reply to the pending call
            }

            "startRecording" -> {
                startRecording()
                result.success(isRecording.get())
            }

            "stopRecording" -> {
                stopRecording()
                result.success(null)
            }

            else -> result.notImplemented() // Unsupported method
        }
    }

    override fun onDestroy() {
        audioTrack?.stop()
        audioTrack?.release()
        audioTrack = null
        stopRecording()
        synchronized(recordingLock) {
            audioRecord?.release()
            audioRecord = null
        }
        bluetoothBridge?.dispose()
        bluetoothBridge = null
        super.onDestroy()
    }
}

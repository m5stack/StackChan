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
import android.util.Log
import androidx.annotation.RequiresPermission
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

    private val SAMPLE_RATE = 16000
    private val CHANNEL_CONFIG = AudioFormat.CHANNEL_OUT_MONO
    private val AUDIO_FORMAT = AudioFormat.ENCODING_PCM_16BIT
    private var audioTrack: AudioTrack? = null

    private val RECORD_CHANNEL_CONFIG = AudioFormat.CHANNEL_IN_MONO
    private var audioRecord: AudioRecord? = null
    private val isRecording = AtomicBoolean(false)
    private var recordBufferSize = 0
    private var eventSink: EventChannel.EventSink? = null

    @RequiresPermission(Manifest.permission.RECORD_AUDIO)
    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)

        val messenger = flutterEngine.dartExecutor.binaryMessenger

        // translated comment
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
        recordBufferSize =
            AudioRecord.getMinBufferSize(SAMPLE_RATE, RECORD_CHANNEL_CONFIG, AUDIO_FORMAT)
        audioRecord = AudioRecord(
            android.media.MediaRecorder.AudioSource.MIC,
            SAMPLE_RATE,
            RECORD_CHANNEL_CONFIG,
            AUDIO_FORMAT,
            recordBufferSize
        )
    }

    private fun playAudio(buffer: ByteBuffer) {
        val data = ByteArray(buffer.remaining())
        buffer.get(data)
        if (audioTrack?.playState != AudioTrack.PLAYSTATE_PLAYING) {
            audioTrack?.play()
        }
        audioTrack?.write(data, 0, data.size)
    }

    // ====================== 1 ======================
    @RequiresPermission(Manifest.permission.RECORD_AUDIO)
    private fun startRecording() {
        if (isRecording.get()) return

        if (audioRecord == null || audioRecord?.state == AudioRecord.STATE_UNINITIALIZED) {
            initAudioRecorder()
        }

        if (audioRecord?.state == AudioRecord.STATE_INITIALIZED) {
            audioRecord?.startRecording()
            isRecording.set(true)

            Thread {
                val buffer = ByteArray(recordBufferSize)
                while (isRecording.get()) {
                    val readSize = audioRecord?.read(buffer, 0, buffer.size) ?: 0
                    if (readSize > 0) {
                        val data = buffer.copyOf(readSize)
                        // translated comment
                        runOnUiThread {
                            eventSink?.success(data)
                        }
                    }
                }
            }.start()
        }
    }

    private fun stopRecording() {
        isRecording.set(false)
        audioRecord?.stop()
    }

    // ====================== 2 ======================
    @RequiresPermission(Manifest.permission.RECORD_AUDIO)
    private fun methodCallHandler(call: MethodCall, result: MethodChannel.Result) {
        when (call.method) {
            "stopPlayPCM" -> {
                if (audioTrack?.playState == AudioTrack.PLAYSTATE_PLAYING) {
                    audioTrack?.pause()
                }
                result.success(null) // translated comment
            }

            "startRecording" -> {
                startRecording()
                result.success(null)
            }

            "stopRecording" -> {
                stopRecording()
                result.success(null)
            }

            else -> result.notImplemented() // translated comment
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        audioTrack?.stop()
        audioTrack?.release()
        audioTrack = null
        stopRecording()
        audioRecord?.release()
        audioRecord = null
    }
}
/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package com.m5stack.stackchan.bluetooth

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import io.flutter.plugin.common.BinaryMessenger
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.util.Locale
import java.util.UUID

class NativeBluetoothBridge(
    private val activity: Activity,
    messenger: BinaryMessenger
) : MethodChannel.MethodCallHandler, EventChannel.StreamHandler {

    private val methodChannel = MethodChannel(messenger, METHOD_CHANNEL_NAME)
    private val eventChannel = EventChannel(messenger, EVENT_CHANNEL_NAME)
    private val mainHandler = Handler(Looper.getMainLooper())

    private var eventSink: EventChannel.EventSink? = null
    private var isReceiverRegistered = false
    private var isScanning = false

    private val devices = mutableMapOf<String, BluetoothDevice>()
    private val gatts = mutableMapOf<String, BluetoothGatt>()
    private val pendingConnect = mutableMapOf<String, PendingCall>()
    private val pendingDiscover = mutableMapOf<String, PendingCall>()
    private val pendingNotify = mutableMapOf<String, PendingCall>()
    private val pendingWrite = mutableMapOf<String, PendingCall>()
    private val pendingMtu = mutableMapOf<String, PendingCall>()
    private val pendingRssi = mutableMapOf<String, PendingCall>()

    private val bluetoothManager: BluetoothManager?
        get() = activity.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager

    private val bluetoothAdapter: BluetoothAdapter?
        get() = bluetoothManager?.adapter

    private val stateReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action == BluetoothAdapter.ACTION_STATE_CHANGED) {
                sendEvent(
                    mapOf(
                        "type" to "adapterStateChanged",
                        "state" to adapterStateString()
                    )
                )
            }
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            sendScanResult(result)
        }

        override fun onBatchScanResults(results: MutableList<ScanResult>) {
            results.forEach { sendScanResult(it) }
        }

        override fun onScanFailed(errorCode: Int) {
            isScanning = false
            sendEvent(
                mapOf(
                    "type" to "scanFailed",
                    "code" to errorCode
                )
            )
        }
    }

    fun setup() {
        methodChannel.setMethodCallHandler(this)
        eventChannel.setStreamHandler(this)
        registerStateReceiver()
    }

    fun dispose() {
        stopScanInternal()
        pendingConnect.keys.toList().forEach { failPending(pendingConnect, it, "disposed", "Bluetooth bridge disposed") }
        pendingDiscover.keys.toList().forEach { failPending(pendingDiscover, it, "disposed", "Bluetooth bridge disposed") }
        pendingNotify.keys.toList().forEach { failPending(pendingNotify, it, "disposed", "Bluetooth bridge disposed") }
        pendingWrite.keys.toList().forEach { failPending(pendingWrite, it, "disposed", "Bluetooth bridge disposed") }
        pendingMtu.keys.toList().forEach { failPending(pendingMtu, it, "disposed", "Bluetooth bridge disposed") }
        pendingRssi.keys.toList().forEach { failPending(pendingRssi, it, "disposed", "Bluetooth bridge disposed") }
        gatts.values.forEach { it.close() }
        gatts.clear()
        unregisterStateReceiver()
        methodChannel.setMethodCallHandler(null)
        eventChannel.setStreamHandler(null)
    }

    override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
        eventSink = events
        sendEvent(
            mapOf(
                "type" to "adapterStateChanged",
                "state" to adapterStateString()
            )
        )
    }

    override fun onCancel(arguments: Any?) {
        eventSink = null
    }

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        try {
            when (call.method) {
                "getAdapterState" -> result.success(adapterStateString())
                "turnOn" -> turnOn(result)
                "startScan" -> startScan(call, result)
                "stopScan" -> {
                    stopScanInternal()
                    result.success(null)
                }
                "connect" -> connect(call, result)
                "disconnect" -> disconnect(call, result)
                "discoverServices" -> discoverServices(call, result)
                "setNotifyValue" -> setNotifyValue(call, result)
                "writeCharacteristic" -> writeCharacteristic(call, result)
                "requestMtu" -> requestMtu(call, result)
                "readRssi" -> readRssi(call, result)
                else -> result.notImplemented()
            }
        } catch (error: SecurityException) {
            result.error("permission_denied", error.localizedMessage, null)
        } catch (error: IllegalArgumentException) {
            result.error("invalid_argument", error.localizedMessage, null)
        } catch (error: Throwable) {
            result.error("bluetooth_error", error.localizedMessage, null)
        }
    }

    @SuppressLint("MissingPermission")
    private fun turnOn(result: MethodChannel.Result) {
        val adapter = bluetoothAdapter ?: run {
            result.success(false)
            return
        }
        if (adapter.isEnabled) {
            result.success(true)
            return
        }
        if (!hasConnectPermission()) {
            result.error("permission_denied", "BLUETOOTH_CONNECT permission is required", null)
            return
        }
        @Suppress("DEPRECATION")
        val requested = adapter.enable()
        result.success(requested || adapter.isEnabled)
    }

    @SuppressLint("MissingPermission")
    private fun startScan(call: MethodCall, result: MethodChannel.Result) {
        val adapter = bluetoothAdapter ?: run {
            result.error("unavailable", "Bluetooth is unavailable", null)
            return
        }
        if (!adapter.isEnabled) {
            result.error("adapter_off", "Bluetooth is off", null)
            return
        }
        if (!hasScanPermission()) {
            result.error("permission_denied", "BLUETOOTH_SCAN permission is required", null)
            return
        }
        val scanner = adapter.bluetoothLeScanner
        if (scanner == null) {
            result.error("scanner_unavailable", "Bluetooth LE scanner is not ready", null)
            return
        }

        stopScanInternal()

        val serviceUuids = call.argument<List<String>>("serviceUuids") ?: emptyList()
        val filters = serviceUuids.mapNotNull { uuidString ->
            runCatching {
                ScanFilter.Builder()
                    .setServiceUuid(ParcelUuid(UUID.fromString(uuidString)))
                    .build()
            }.getOrNull()
        }
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        scanner.startScan(filters, settings, scanCallback)
        isScanning = true
        result.success(null)
    }

    @SuppressLint("MissingPermission")
    private fun stopScanInternal() {
        if (!isScanning) return
        if (hasScanPermission()) {
            bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
        }
        isScanning = false
    }

    @SuppressLint("MissingPermission")
    private fun connect(call: MethodCall, result: MethodChannel.Result) {
        val deviceId = requiredString(call, "deviceId")
        val timeoutMillis = call.argument<Number>("timeoutMillis")?.toLong() ?: DEFAULT_CONNECT_TIMEOUT_MS
        val autoConnect = call.argument<Boolean>("autoConnect") ?: false
        if (!hasConnectPermission()) {
            result.error("permission_denied", "BLUETOOTH_CONNECT permission is required", null)
            return
        }
        val device = devices[deviceId] ?: bluetoothAdapter?.getRemoteDevice(deviceId)
        if (device == null) {
            result.error("not_found", "Bluetooth device was not found", null)
            return
        }
        devices[deviceId] = device

        gatts[deviceId]?.let { existingGatt ->
            if (bluetoothManager?.getConnectionState(device, BluetoothProfile.GATT) == BluetoothProfile.STATE_CONNECTED) {
                sendConnectionState(deviceId, "connected", null)
                result.success(null)
                return
            }
            existingGatt.close()
            gatts.remove(deviceId)
        }

        val timeoutRunnable = Runnable {
            pendingConnect.remove(deviceId)?.let {
                it.result.error("timeout", "Bluetooth connection timed out", null)
            }
            gatts[deviceId]?.disconnect()
            gatts[deviceId]?.close()
            gatts.remove(deviceId)
        }
        pendingConnect[deviceId] = PendingCall(result, timeoutRunnable)
        mainHandler.postDelayed(timeoutRunnable, timeoutMillis)

        val callback = BridgeGattCallback(deviceId)
        val gatt = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            device.connectGatt(activity, autoConnect, callback, BluetoothDevice.TRANSPORT_LE)
        } else {
            @Suppress("DEPRECATION")
            device.connectGatt(activity, autoConnect, callback)
        }
        if (gatt == null) {
            failPending(pendingConnect, deviceId, "connect_failed", "Failed to create GATT connection")
        } else {
            gatts[deviceId] = gatt
        }
    }

    @SuppressLint("MissingPermission")
    private fun disconnect(call: MethodCall, result: MethodChannel.Result) {
        val deviceId = requiredString(call, "deviceId")
        val gatt = gatts[deviceId]
        if (gatt == null) {
            result.success(null)
            return
        }
        if (hasConnectPermission()) {
            gatt.disconnect()
        }
        result.success(null)
    }

    @SuppressLint("MissingPermission")
    private fun discoverServices(call: MethodCall, result: MethodChannel.Result) {
        val deviceId = requiredString(call, "deviceId")
        val timeoutMillis = call.argument<Number>("timeoutMillis")?.toLong() ?: DEFAULT_DISCOVER_TIMEOUT_MS
        val gatt = connectedGatt(deviceId, result) ?: return

        val timeoutRunnable = Runnable {
            pendingDiscover.remove(deviceId)?.result?.error("timeout", "Service discovery timed out", null)
        }
        pendingDiscover[deviceId] = PendingCall(result, timeoutRunnable)
        mainHandler.postDelayed(timeoutRunnable, timeoutMillis)

        if (!gatt.discoverServices()) {
            failPending(pendingDiscover, deviceId, "discover_failed", "Failed to start service discovery")
        }
    }

    @SuppressLint("MissingPermission")
    private fun setNotifyValue(call: MethodCall, result: MethodChannel.Result) {
        val deviceId = requiredString(call, "deviceId")
        val serviceUuid = requiredString(call, "serviceUuid")
        val characteristicUuid = requiredString(call, "characteristicUuid")
        val enabled = call.argument<Boolean>("enabled") ?: false
        val gatt = connectedGatt(deviceId, result) ?: return
        val characteristic = findCharacteristic(gatt, serviceUuid, characteristicUuid) ?: run {
            result.error("not_found", "Characteristic was not found", null)
            return
        }

        val localSuccess = gatt.setCharacteristicNotification(characteristic, enabled)
        val descriptor = characteristic.getDescriptor(CLIENT_CHARACTERISTIC_CONFIG_UUID)
        if (descriptor == null) {
            result.success(localSuccess)
            return
        }

        val descriptorValue = when {
            !enabled -> BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE
            characteristic.properties and BluetoothGattCharacteristic.PROPERTY_INDICATE != 0 ->
                BluetoothGattDescriptor.ENABLE_INDICATION_VALUE
            else -> BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        }
        val key = operationKey(deviceId, serviceUuid, characteristicUuid)
        val timeoutRunnable = Runnable {
            pendingNotify.remove(key)?.result?.error("timeout", "Descriptor write timed out", null)
        }
        pendingNotify[key] = PendingCall(result, timeoutRunnable)
        mainHandler.postDelayed(timeoutRunnable, DEFAULT_OPERATION_TIMEOUT_MS)

        val started = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            gatt.writeDescriptor(descriptor, descriptorValue) == BluetoothGatt.GATT_SUCCESS
        } else {
            @Suppress("DEPRECATION")
            descriptor.value = descriptorValue
            @Suppress("DEPRECATION")
            gatt.writeDescriptor(descriptor)
        }
        if (!started) {
            failPending(pendingNotify, key, "notify_failed", "Failed to write notification descriptor")
        }
    }

    @SuppressLint("MissingPermission")
    private fun writeCharacteristic(call: MethodCall, result: MethodChannel.Result) {
        val deviceId = requiredString(call, "deviceId")
        val serviceUuid = requiredString(call, "serviceUuid")
        val characteristicUuid = requiredString(call, "characteristicUuid")
        val value = call.argument<ByteArray>("value") ?: ByteArray(0)
        val withoutResponse = call.argument<Boolean>("withoutResponse") ?: false
        val gatt = connectedGatt(deviceId, result) ?: return
        val characteristic = findCharacteristic(gatt, serviceUuid, characteristicUuid) ?: run {
            result.error("not_found", "Characteristic was not found", null)
            return
        }

        val supportsWrite = characteristic.properties and BluetoothGattCharacteristic.PROPERTY_WRITE != 0
        val supportsWriteNoResponse = characteristic.properties and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE != 0
        val writeType = if (withoutResponse || (!supportsWrite && supportsWriteNoResponse)) {
            BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
        } else {
            BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        }
        val key = operationKey(deviceId, serviceUuid, characteristicUuid)
        val timeoutRunnable = Runnable {
            pendingWrite.remove(key)?.result?.error("timeout", "Characteristic write timed out", null)
        }
        pendingWrite[key] = PendingCall(result, timeoutRunnable)
        mainHandler.postDelayed(timeoutRunnable, DEFAULT_OPERATION_TIMEOUT_MS)

        val started = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            gatt.writeCharacteristic(characteristic, value, writeType) == BluetoothGatt.GATT_SUCCESS
        } else {
            characteristic.writeType = writeType
            @Suppress("DEPRECATION")
            characteristic.value = value
            @Suppress("DEPRECATION")
            gatt.writeCharacteristic(characteristic)
        }
        if (!started) {
            failPending(pendingWrite, key, "write_failed", "Failed to write characteristic")
            return
        }
        if (writeType == BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE) {
            completePending(pendingWrite, key, true)
        }
    }

    @SuppressLint("MissingPermission")
    private fun requestMtu(call: MethodCall, result: MethodChannel.Result) {
        val deviceId = requiredString(call, "deviceId")
        val mtu = call.argument<Number>("mtu")?.toInt() ?: DEFAULT_MTU
        val gatt = connectedGatt(deviceId, result) ?: return
        val timeoutRunnable = Runnable {
            pendingMtu.remove(deviceId)?.result?.error("timeout", "MTU request timed out", null)
        }
        pendingMtu[deviceId] = PendingCall(result, timeoutRunnable)
        mainHandler.postDelayed(timeoutRunnable, DEFAULT_OPERATION_TIMEOUT_MS)
        if (!gatt.requestMtu(mtu)) {
            failPending(pendingMtu, deviceId, "mtu_failed", "Failed to request MTU")
        }
    }

    @SuppressLint("MissingPermission")
    private fun readRssi(call: MethodCall, result: MethodChannel.Result) {
        val deviceId = requiredString(call, "deviceId")
        val timeoutMillis = call.argument<Number>("timeoutMillis")?.toLong() ?: DEFAULT_OPERATION_TIMEOUT_MS
        val gatt = connectedGatt(deviceId, result) ?: return
        val timeoutRunnable = Runnable {
            pendingRssi.remove(deviceId)?.result?.error("timeout", "RSSI read timed out", null)
        }
        pendingRssi[deviceId] = PendingCall(result, timeoutRunnable)
        mainHandler.postDelayed(timeoutRunnable, timeoutMillis)
        if (!gatt.readRemoteRssi()) {
            failPending(pendingRssi, deviceId, "rssi_failed", "Failed to read RSSI")
        }
    }

    private inner class BridgeGattCallback(private val deviceId: String) : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    gatts[deviceId] = gatt
                    devices[deviceId] = gatt.device
                    sendConnectionState(deviceId, "connected", null)
                    completePending(pendingConnect, deviceId, null)
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    val reason = if (status == BluetoothGatt.GATT_SUCCESS) null else "status=$status"
                    sendConnectionState(deviceId, "disconnected", reason)
                    failPending(pendingConnect, deviceId, "disconnected", reason ?: "Bluetooth disconnected")
                    gatt.close()
                    gatts.remove(deviceId)
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                completePending(pendingDiscover, deviceId, serviceMaps(gatt))
            } else {
                failPending(pendingDiscover, deviceId, "discover_failed", "Service discovery failed: $status")
            }
        }

        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int
        ) {
            val characteristic = descriptor.characteristic ?: return
            val service = characteristic.service ?: return
            val key = operationKey(deviceId, service.uuid.toString(), characteristic.uuid.toString())
            if (status == BluetoothGatt.GATT_SUCCESS) {
                completePending(pendingNotify, key, true)
            } else {
                failPending(pendingNotify, key, "notify_failed", "Descriptor write failed: $status")
            }
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            val service = characteristic.service ?: return
            val key = operationKey(deviceId, service.uuid.toString(), characteristic.uuid.toString())
            if (status == BluetoothGatt.GATT_SUCCESS) {
                completePending(pendingWrite, key, true)
            } else {
                failPending(pendingWrite, key, "write_failed", "Characteristic write failed: $status")
            }
        }

        @Deprecated("Deprecated in Android 13")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            @Suppress("DEPRECATION")
            sendCharacteristicValue(gatt, characteristic, characteristic.value ?: ByteArray(0))
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            sendCharacteristicValue(gatt, characteristic, value)
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                completePending(pendingMtu, deviceId, mtu)
            } else {
                failPending(pendingMtu, deviceId, "mtu_failed", "MTU request failed: $status")
            }
        }

        override fun onReadRemoteRssi(gatt: BluetoothGatt, rssi: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                completePending(pendingRssi, deviceId, rssi)
            } else {
                failPending(pendingRssi, deviceId, "rssi_failed", "RSSI read failed: $status")
            }
        }
    }

    private fun sendScanResult(result: ScanResult) {
        val deviceId = result.device.address
        devices[deviceId] = result.device
        sendEvent(
            mapOf(
                "type" to "scanResult",
                "result" to scanResultMap(result)
            )
        )
    }

    @SuppressLint("MissingPermission")
    private fun scanResultMap(result: ScanResult): Map<String, Any?> {
        val record = result.scanRecord
        val safeName = record?.deviceName ?: safeDeviceName(result.device)
        val serviceData = mutableMapOf<String, ByteArray>()
        record?.serviceData?.forEach { (uuid, bytes) ->
            serviceData[uuid.uuid.toString()] = bytes
        }
        val manufacturerData = mutableMapOf<Int, ByteArray>()
        record?.manufacturerSpecificData?.let { sparseArray ->
            for (index in 0 until sparseArray.size()) {
                manufacturerData[sparseArray.keyAt(index)] = sparseArray.valueAt(index)
            }
        }
        val txPowerLevel = record?.txPowerLevel?.takeIf { it != Int.MIN_VALUE }
        return mapOf(
            "device" to mapOf(
                "id" to result.device.address,
                "name" to safeName,
                "advName" to safeName
            ),
            "advertisementData" to mapOf(
                "advName" to safeName,
                "txPowerLevel" to txPowerLevel,
                "connectable" to result.isConnectable,
                "serviceUuids" to (record?.serviceUuids?.map { it.uuid.toString() } ?: emptyList<String>()),
                "serviceData" to serviceData,
                "manufacturerData" to manufacturerData
            ),
            "rssi" to result.rssi
        )
    }

    private fun serviceMaps(gatt: BluetoothGatt): List<Map<String, Any?>> {
        return gatt.services.map { service -> serviceMap(service) }
    }

    private fun serviceMap(service: BluetoothGattService): Map<String, Any?> {
        return mapOf(
            "uuid" to service.uuid.toString(),
            "characteristics" to service.characteristics.map { characteristicMap(it) }
        )
    }

    private fun characteristicMap(characteristic: BluetoothGattCharacteristic): Map<String, Any?> {
        val properties = characteristic.properties
        return mapOf(
            "uuid" to characteristic.uuid.toString(),
            "properties" to mapOf(
                "read" to (properties and BluetoothGattCharacteristic.PROPERTY_READ != 0),
                "write" to (properties and BluetoothGattCharacteristic.PROPERTY_WRITE != 0),
                "writeWithoutResponse" to (properties and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE != 0),
                "notify" to (properties and BluetoothGattCharacteristic.PROPERTY_NOTIFY != 0),
                "indicate" to (properties and BluetoothGattCharacteristic.PROPERTY_INDICATE != 0)
            )
        )
    }

    private fun sendCharacteristicValue(
        gatt: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic,
        value: ByteArray
    ) {
        val service = characteristic.service ?: return
        sendEvent(
            mapOf(
                "type" to "characteristicChanged",
                "deviceId" to gatt.device.address,
                "serviceUuid" to service.uuid.toString(),
                "characteristicUuid" to characteristic.uuid.toString(),
                "value" to value
            )
        )
    }

    private fun sendConnectionState(deviceId: String, state: String, reason: String?) {
        sendEvent(
            mapOf(
                "type" to "connectionStateChanged",
                "deviceId" to deviceId,
                "state" to state,
                "reason" to reason
            )
        )
    }

    private fun connectedGatt(deviceId: String, result: MethodChannel.Result): BluetoothGatt? {
        val gatt = gatts[deviceId]
        if (gatt == null) {
            result.error("not_connected", "Bluetooth device is not connected", null)
            return null
        }
        return gatt
    }

    private fun findCharacteristic(
        gatt: BluetoothGatt,
        serviceUuid: String,
        characteristicUuid: String
    ): BluetoothGattCharacteristic? {
        val service = runCatching { gatt.getService(UUID.fromString(serviceUuid)) }.getOrNull()
            ?: gatt.services.firstOrNull { it.uuid.toString().equals(serviceUuid, ignoreCase = true) }
        return service?.characteristics?.firstOrNull {
            it.uuid.toString().equals(characteristicUuid, ignoreCase = true)
        }
    }

    private fun requiredString(call: MethodCall, key: String): String {
        return call.argument<String>(key)?.takeIf { it.isNotBlank() }
            ?: throw IllegalArgumentException("Missing required argument: $key")
    }

    private fun operationKey(deviceId: String, serviceUuid: String, characteristicUuid: String): String {
        return listOf(deviceId, serviceUuid.lowercase(Locale.US), characteristicUuid.lowercase(Locale.US)).joinToString("|")
    }

    private fun completePending(map: MutableMap<String, PendingCall>, key: String, value: Any?) {
        val pending = map.remove(key) ?: return
        mainHandler.removeCallbacks(pending.timeoutRunnable)
        mainHandler.post { pending.result.success(value) }
    }

    private fun failPending(map: MutableMap<String, PendingCall>, key: String, code: String, message: String) {
        val pending = map.remove(key) ?: return
        mainHandler.removeCallbacks(pending.timeoutRunnable)
        mainHandler.post { pending.result.error(code, message, null) }
    }

    private fun sendEvent(event: Map<String, Any?>) {
        mainHandler.post { eventSink?.success(event) }
    }

    private fun adapterStateString(): String {
        val adapter = bluetoothAdapter ?: return "unavailable"
        return when (adapter.state) {
            BluetoothAdapter.STATE_OFF -> "off"
            BluetoothAdapter.STATE_TURNING_ON -> "turningOn"
            BluetoothAdapter.STATE_ON -> "on"
            BluetoothAdapter.STATE_TURNING_OFF -> "turningOff"
            else -> "unknown"
        }
    }

    private fun safeDeviceName(device: BluetoothDevice): String {
        return if (hasConnectPermission()) {
            @SuppressLint("MissingPermission")
            device.name ?: ""
        } else {
            ""
        }
    }

    private fun hasScanPermission(): Boolean {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
            activity.checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
    }

    private fun hasConnectPermission(): Boolean {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
            activity.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
    }

    private fun registerStateReceiver() {
        if (isReceiverRegistered) return
        val filter = IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            activity.registerReceiver(stateReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            activity.registerReceiver(stateReceiver, filter)
        }
        isReceiverRegistered = true
    }

    private fun unregisterStateReceiver() {
        if (!isReceiverRegistered) return
        activity.unregisterReceiver(stateReceiver)
        isReceiverRegistered = false
    }

    private data class PendingCall(
        val result: MethodChannel.Result,
        val timeoutRunnable: Runnable
    )

    companion object {
        private const val METHOD_CHANNEL_NAME = "com.m5stack.stackchan/bluetooth"
        private const val EVENT_CHANNEL_NAME = "com.m5stack.stackchan/bluetooth_events"
        private const val DEFAULT_CONNECT_TIMEOUT_MS = 15_000L
        private const val DEFAULT_DISCOVER_TIMEOUT_MS = 35_000L
        private const val DEFAULT_OPERATION_TIMEOUT_MS = 15_000L
        private const val DEFAULT_MTU = 247
        private val CLIENT_CHARACTERISTIC_CONFIG_UUID: UUID =
            UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}

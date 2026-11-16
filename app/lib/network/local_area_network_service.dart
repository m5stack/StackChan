/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:flutter/foundation.dart' show debugPrint, visibleForTesting;
import 'package:stack_chan/model/msg_type.dart';
import 'package:stack_chan/util/mac_address_validator.dart';
import 'package:stack_chan/util/rsa_util.dart';

/// LAN frames use a separate 20-byte header and cannot go through the remote WebSocket's 5-byte decoder.
class LocalAreaNetworkMessage {
  const LocalAreaNetworkMessage({
    required this.type,
    required this.flags,
    required this.sessionId,
    required this.sequence,
    required this.payload,
    this.webSocketKey,
  });

  final MsgType type;
  final int flags;
  final int sessionId;
  final int sequence;
  final Uint8List payload;

  /// RSA handshake plaintext from the inbound WebSocket, used only for authentication, not application encryption.
  final String? webSocketKey;

  static const headerSize = 20;
  static const maxPayloadSize = 0xffff;

  /// Encodes SC / version / type / flags / session / seq / length in network byte order.
  Uint8List encode() {
    if (payload.length > maxPayloadSize) {
      throw ArgumentError('LAN payload exceeds 65535 bytes');
    }
    final bytes = Uint8List(headerSize + payload.length);
    final header = ByteData.sublistView(bytes);
    bytes.setAll(0, [0x53, 0x43, 1, type.value]);
    header.setUint16(4, flags);
    header.setUint64(6, sessionId);
    header.setUint32(14, sequence);
    header.setUint16(18, payload.length);
    bytes.setAll(headerSize, payload);
    return bytes;
  }

  /// Drops frames with unknown versions/types or mismatched lengths so corrupted data never reaches the UI.
  static LocalAreaNetworkMessage? decode(
    Uint8List bytes, {
    String? webSocketKey,
  }) {
    if (bytes.length < headerSize ||
        bytes[0] != 0x53 ||
        bytes[1] != 0x43 ||
        bytes[2] != 1) {
      return null;
    }
    final header = ByteData.sublistView(bytes);
    final length = header.getUint16(18);
    if (bytes.length != headerSize + length) return null;
    MsgType? type;
    for (final candidate in MsgType.values) {
      if (candidate.value == bytes[3]) type = candidate;
    }
    if (type == null) return null;
    return LocalAreaNetworkMessage(
      type: type,
      flags: header.getUint16(4),
      sessionId: header.getUint64(6),
      sequence: header.getUint32(14),
      payload: Uint8List.sublistView(bytes, headerSize),
      webSocketKey: webSocketKey,
    );
  }
}

/// LAN server endpoint on the phone, delivered to the firmware over the remote channel.
///
/// The firmware connects directly to this service. Addresses contain only routable IPv4;
/// loopback and 169.254/16 link-local addresses are excluded.
class LanServerEndpoint {
  const LanServerEndpoint({required this.port, required this.addresses});

  final int port;
  final List<String> addresses;

  Map<String, dynamic> toJson() => {
    'port': port,
    'path': LocalAreaNetworkService.webSocketPath,
    'ip': addresses,
  };

  /// Remote-channel payload: no MAC prefix; the server routes by the connection's MAC.
  Uint8List encode() => Uint8List.fromList(utf8.encode(jsonEncode(toJson())));
}

/// Publishes a WebSocket service on the LAN and accepts connections initiated by the firmware.
///
/// The firmware receives the endpoint in an `AppLanAddress` message; DNS-SD/mDNS discovery is not used.
/// Application messages remain plaintext and keep the target MAC prefix.
class LocalAreaNetworkService {
  LocalAreaNetworkService._() : _decrypt = RsaUtil.decryptStackChanBlue;

  @visibleForTesting
  LocalAreaNetworkService.forTesting(this._decrypt);

  static final instance = LocalAreaNetworkService._();

  static const webSocketPath = '/ws';

  final String Function(String) _decrypt;
  final _messages = StreamController<LocalAreaNetworkMessage>.broadcast();
  final _connections = StreamController<bool>.broadcast();
  final _endpoints = StreamController<LanServerEndpoint?>.broadcast();

  HttpServer? _server;
  WebSocket? _socket;
  StreamSubscription<dynamic>? _subscription;
  Timer? _retryTimer;
  Future<void> _lifecycle = Future<void>.value();
  String _desiredTarget = '';
  String _runningTarget = '';
  bool _foreground = true;
  int _pageHolders = 0;
  bool _accepting = false;
  bool _disposed = false;
  LanServerEndpoint? _endpoint;
  int _generation = 0;

  /// Generation the running server was bound with; if a newer generation exists,
  /// in-flight requests would be rejected, so the server must rebind.
  int _boundGeneration = -1;
  int _sessionId = 0;
  int _sequence = 0;

  Stream<LocalAreaNetworkMessage> get messages => _messages.stream;

  Stream<bool> get connectionStates => _connections.stream;

  /// Emits the endpoint when the local service is ready and null when it stops.
  Stream<LanServerEndpoint?> get serverEndpoints => _endpoints.stream;

  /// Currently available endpoint, or null when the service is not running.
  LanServerEndpoint? get serverEndpoint => _endpoint;

  String get targetDeviceId => _desiredTarget;

  int? get localWebSocketPort => _server?.port;

  /// Serve only while a page holds the service, the app is foregrounded, and a target is set.
  bool get _shouldServe =>
      _foreground && _pageHolders > 0 && _desiredTarget.isNotEmpty;

  /// True while any of the three target pages (AVATAR / MOTION / MONITORING CAMERA) is open.
  bool get isPageScoped => _pageHolders > 0;

  /// Disables the old channel immediately on target switch, page exit, or backgrounding, even if async start/stop is pending.
  bool get hasConnectedDevice =>
      !_disposed &&
      _shouldServe &&
      _runningTarget == _desiredTarget &&
      _socket?.readyState == WebSocket.open;

  bool canSendTo(String mac) =>
      hasConnectedDevice && MacAddressValidator.areEqual(mac, _runningTarget);

  /// Target MAC updates come from restore, bind, switch, and unbind; serialized start/stop keeps old connections off new targets.
  Future<void> setTargetDevice(String mac) {
    if (_disposed) return Future<void>.value();
    final target = MacAddressValidator.isValidMac(mac)
        ? MacAddressValidator.normalize(mac)!
        : '';
    if (target != _desiredTarget) {
      _desiredTarget = target;
      _generation++;
      _dropConnection();
    }
    _updateRetryTimer();
    return _enqueueReconcile();
  }

  /// Called on page entry: keep the local service and connection alive until the page exits.
  Future<void> retainForPage() {
    if (_disposed) return Future<void>.value();
    _pageHolders++;
    _updateRetryTimer();
    return _enqueueReconcile();
  }

  /// Called on page exit: close the local service and disconnect the device once the last page leaves.
  Future<void> releaseForPage() {
    if (_disposed || _pageHolders == 0) return Future<void>.value();
    _pageHolders--;
    if (_pageHolders == 0) {
      _generation++;
      _dropConnection();
    }
    _updateRetryTimer();
    return _enqueueReconcile();
  }

  /// Stops the local service and connection in the background and restores them with the latest target on resume.
  Future<void> setForeground(bool foreground) {
    if (_disposed || _foreground == foreground) return _lifecycle;
    _foreground = foreground;
    _generation++;
    _dropConnection();
    _updateRetryTimer();
    return _enqueueReconcile();
  }

  /// Retries only when the service is page-held, foregrounded, and targeted; otherwise stops the timer.
  void _updateRetryTimer() {
    if (!_shouldServe) {
      _retryTimer?.cancel();
      _retryTimer = null;
      return;
    }
    _retryTimer ??= Timer.periodic(const Duration(seconds: 10), (_) {
      if (!_disposed) unawaited(_enqueueReconcile());
    });
  }

  /// True means the frame was handed to the WebSocket; callers fall back to the cloud when disconnected or encoding fails.
  bool sendConnectedMessage(
    MsgType type, {
    required String deviceMac,
    Uint8List? payload,
  }) {
    if (!canSendTo(deviceMac)) return false;
    try {
      final packet = LocalAreaNetworkMessage(
        type: type,
        flags: 0,
        sessionId: _sessionId,
        sequence: _sequence = (_sequence + 1) & 0xffffffff,
        payload: payload ?? Uint8List(0),
      ).encode();
      _socket!.add(packet);
      return true;
    } on ArgumentError {
      // Media frames larger than the LAN uint16 limit can still be sent over the remote channel.
      return false;
    } catch (_) {
      _dropConnection();
      return false;
    }
  }

  /// Serializes asynchronous start/stop; failures affect only LAN, and the timer keeps retrying.
  Future<void> _enqueueReconcile() {
    _lifecycle = _lifecycle.then((_) async {
      if (_disposed) return;
      try {
        await _reconcile();
      } catch (error) {
        await _stop();
        debugPrint('LAN service unavailable: $error');
      }
    });
    return _lifecycle;
  }

  Future<void> _reconcile() async {
    final target = _shouldServe ? _desiredTarget : '';
    if (target == _runningTarget &&
        (target.isEmpty ||
            (_server != null && _boundGeneration == _generation))) {
      return;
    }
    await _stop();
    if (target.isEmpty || target != _desiredTarget || !_foreground) return;
    final generation = _generation;
    final server = await HttpServer.bind(InternetAddress.anyIPv4, 0);
    _server = server;
    _runningTarget = target;
    _boundGeneration = generation;
    debugPrint('LAN server bound target=$target port=${server.port}');
    final random = Random.secure();
    _sessionId =
        (random.nextInt(0x7fffffff) << 32) | random.nextInt(0x100000000);
    _sequence = 0;
    server.listen(
      (request) => unawaited(_handleRequest(request, generation)),
      onError: (Object error) {
        if (generation != _generation) return;
        debugPrint('LAN server error: $error');
      },
    );
    await _announceEndpoint(server.port, generation);
  }

  /// Hands the endpoint to the caller when the listener is ready for delivery to the firmware.
  ///
  /// An address lookup failure only skips this announcement; the established local service is unaffected.
  Future<void> _announceEndpoint(int port, int generation) async {
    final addresses = await _localIpv4Addresses();
    if (_disposed || generation != _generation || _server == null) return;
    if (addresses.isEmpty) {
      debugPrint(
        'LAN endpoint has no routable IPv4 address; announcement skipped',
      );
      return;
    }
    _endpoint = LanServerEndpoint(port: port, addresses: addresses);
    debugPrint(
      'LAN endpoint announced port=$port addresses=${addresses.join(',')}',
    );
    if (!_endpoints.isClosed) _endpoints.add(_endpoint);
  }

  /// Routable IPv4 addresses, private ranges first so the firmware can try them in order.
  static Future<List<String>> _localIpv4Addresses() async {
    final addresses = <String>[];
    try {
      final interfaces = await NetworkInterface.list(
        type: InternetAddressType.IPv4,
        includeLoopback: false,
        includeLinkLocal: false,
      );
      for (final interface in interfaces) {
        for (final address in interface.addresses) {
          if (address.isLoopback) continue;
          if (!addresses.contains(address.address)) {
            addresses.add(address.address);
          }
        }
      }
    } catch (_) {
      return const [];
    }
    addresses.sort((a, b) => _privateRank(a).compareTo(_privateRank(b)));
    return addresses;
  }

  static int _privateRank(String address) {
    if (address.startsWith('192.168.')) return 0;
    if (address.startsWith('10.')) return 1;
    if (RegExp(r'^172\.(1[6-9]|2[0-9]|3[01])\.').hasMatch(address)) return 2;
    return 3;
  }

  /// Decrypts the Bluetooth pairing key and checks the MAC before the WebSocket upgrade, rejecting wrong or concurrent connections.
  Future<void> _handleRequest(HttpRequest request, int generation) async {
    try {
      if (generation != _generation ||
          !_foreground ||
          _runningTarget != _desiredTarget ||
          request.uri.path != webSocketPath ||
          !WebSocketTransformer.isUpgradeRequest(request)) {
        await _reject(request, HttpStatus.notFound);
        return;
      }
      if (_socket != null || _accepting) {
        debugPrint('LAN WebSocket rejected status=409 target=$_runningTarget');
        await _reject(request, HttpStatus.conflict);
        return;
      }
      final authorization = request.headers.value(
        HttpHeaders.authorizationHeader,
      );
      String? key;
      try {
        if (authorization != null) key = _decrypt(authorization);
      } catch (_) {
        // Neither the ciphertext nor the decrypted result is logged.
      }
      final fields = key?.split('|');
      if (fields == null ||
          fields.length != 3 ||
          fields[1].isEmpty ||
          fields[2].isEmpty ||
          !MacAddressValidator.isValidMac(fields[0]) ||
          !MacAddressValidator.areEqual(fields[0], _runningTarget)) {
        debugPrint('LAN WebSocket rejected status=401 target=$_runningTarget');
        await _reject(request, HttpStatus.unauthorized);
        return;
      }
      _accepting = true;
      final socket = await WebSocketTransformer.upgrade(request);
      if (generation == _generation) _accepting = false;
      if (_disposed || generation != _generation || !_foreground) {
        unawaited(socket.close(WebSocketStatus.goingAway));
        return;
      }
      _socket = socket;
      // WebSocket heartbeats detect device power loss and network changes without relying on media traffic.
      socket.pingInterval = const Duration(seconds: 5);
      _subscription = socket.listen(
        (data) => _handleData(socket, data, key!),
        onDone: () => _dropConnection(socket),
        onError: (Object _) => _dropConnection(socket),
        cancelOnError: true,
      );
      _connections.add(true);
      debugPrint('LAN WebSocket connected target=$_runningTarget');
    } catch (_) {
      if (generation == _generation) _accepting = false;
      try {
        await request.response.close();
      } catch (_) {}
    }
  }

  Future<void> _reject(HttpRequest request, int status) async {
    request.response.statusCode = status;
    await request.response.close();
  }

  /// Accepts only binary frames from the current connection; business pings are answered here, never forwarded to the cloud.
  void _handleData(WebSocket socket, dynamic data, String key) {
    if (!identical(_socket, socket) || !hasConnectedDevice) return;
    if (data is! List<int>) return;
    final bytes = data is Uint8List ? data : Uint8List.fromList(data);
    final message = LocalAreaNetworkMessage.decode(bytes, webSocketKey: key);
    if (message == null) return;
    if (message.type == MsgType.ping) {
      sendConnectedMessage(MsgType.pong, deviceMac: _runningTarget);
    } else {
      _messages.add(message);
    }
  }

  /// The identity check prevents a late callback from an old socket from closing a new connection.
  void _dropConnection([WebSocket? expected]) {
    if (expected != null && !identical(expected, _socket)) return;
    final socket = _socket;
    _socket = null;
    unawaited(_subscription?.cancel());
    _subscription = null;
    if (socket != null) {
      unawaited(socket.close(WebSocketStatus.goingAway));
      if (!_connections.isClosed) _connections.add(false);
      debugPrint('LAN WebSocket disconnected target=$_runningTarget');
    }
  }

  Future<void> _stop() async {
    _generation++;
    _accepting = false;
    _runningTarget = '';
    _boundGeneration = -1;
    _dropConnection();
    if (_endpoint != null) {
      _endpoint = null;
      if (!_endpoints.isClosed) _endpoints.add(null);
    }
    final server = _server;
    _server = null;
    await server?.close(force: true);
  }

  /// Full disposal is only for App/controller teardown; page exit does not close the shared service.
  Future<void> dispose() async {
    if (_disposed) return;
    _disposed = true;
    _generation++;
    _retryTimer?.cancel();
    _dropConnection();
    await _lifecycle;
    await _stop();
    await _messages.close();
    await _connections.close();
    await _endpoints.close();
  }
}

/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

/// Application-wide constant values and configuration
///
/// This class contains:
/// - API response keys for JSON parsing
/// - Language support configuration
/// - RSA encryption keys for secure communication
/// - Character sets for random generation
/// - Bluetooth encryption keys
///
/// Backend Configuration Notes:
/// - No key material is stored in this file; keys are injected at build time
/// - The keys are used for RSA-OAEP encryption with SHA-256
/// - Pass them with --dart-define (see the key declarations below)
class ValueConstant {
  // ===========================================================================
  // API Response Keys - Used for JSON parsing from backend responses
  // ===========================================================================

  /// Authentication token key for HTTP headers and storage
  static const String token = "token";

  /// Device-specific token key for device authentication
  static const String deviceToken = "deviceToken";

  /// Authorization header key for Bearer token authentication
  static const String authorization = "Authorization";

  /// Device type identifier key
  static const String deviceType = "deviceType";

  /// MAC address key for device identification
  static const String mac = "mac";

  /// Index key for pagination or list positioning
  static const String index = "index";

  /// Data payload key in API responses
  static const String data = "data";

  /// List key for array responses
  static const String list = "list";

  /// File upload/download identifier key
  static const String file = "file";

  /// Directory path key for file operations
  static const String directory = "directory";

  /// Moments or timeline content key
  static const String moments = "moments";

  /// Name field key for various entities
  static const String name = "name";

  /// Dance choreography data key
  static const String danceData = "danceData";

  /// Dance name/title key
  static const String danceName = "danceName";

  /// Music file URL key
  static const String musicUrl = "musicUrl";

  /// Text content key for messages or posts
  static const String contentText = "content_text";

  /// Image content key for media attachments
  static const String contentImage = "content_image";

  /// Page number key for pagination
  static const String page = "page";

  /// Page size key for pagination results
  static const String pageSize = "pageSize";

  /// Generic ID key for various entities
  static const String id = "id";

  /// Post identifier key for social content
  static const String postId = "postId";

  /// Content body key
  static const String content = "content";

  /// Comment identifier key
  static const String commentId = "commentId";

  /// Random seed key for deterministic operations
  static const String seed = "seed";

  /// Device control mode configuration key
  static const String deviceControlMode = "deviceControlMode";

  /// Login status flag key
  static const String isLogin = "isLogin";

  /// Username field key for authentication
  static const String username = "username";

  /// Password field key for authentication
  static const String password = "password";

  /// Email address field key
  static const String email = "email";

  /// Application version identifier key
  static const String appVersion = "app_version";

  // ===========================================================================
  // Bluetooth BLE Advertisement Data Keys
  // ===========================================================================

  /// Manufacturer specific data key in BLE advertisement
  static const String manufacturerData = "manufacturerData";

  /// Service data key in BLE advertisement
  static const String serviceData = "serviceData";

  /// Service UUID list key in BLE advertisement
  static const String serviceUuids = "serviceUuids";

  /// Connectable flag key in BLE advertisement
  static const String connectable = "connectable";

  /// Transmit power level key in BLE advertisement
  static const String txPowerLevel = "txPowerLevel";

  /// Advertised device name key
  static const String advName = "advName";

  /// Device serial number key for identification
  static const String serialNumber = "serialNumber";

  /// Device MAC address key (alternative)
  static const String deviceMac = "deviceMac";

  /// Device unique identifier key
  static const String deviceId = "deviceId";

  static const String motionPromptWindow = "motionPromptWindow";

  static const String avatarPromptWindow = "avatarPromptWindow";

  static const String monitoringCameraPromptWindow =
      "monitoringCameraPromptWindow";

  static const String dancePromptWindow = "dancePromptWindow";

  static const String agreeUserAgreement = "agreeUserAgreement";

  // ===========================================================================
  // Language Support Configuration
  // ===========================================================================

  /// Supported languages map
  /// Key: Language code (ISO 639-1)
  /// Value: Display name in English and native language
  static const Map<String, String> languages = {
    "en": "English",
    "zh": "Chinese Mandarin 普通话",
    "yue": "Cantonese 粵語",
    "ja": "Japanese 日本語",
    "ko": "Korean 한국어",
    "ru": "Russian Русский",
    "es": "Spanish Español",
    "ar": "Arabic العربية",
    "fr": "French Français",
    "vi": "Vietnamese Tiếng Việt",
    "it": "Italian Italiano",
    "id": "Indonesian Bahasa Indonesia",
    "hi": "Hindi हिन्दी",
    "fi": "Finnish Suomi",
    "th": "Thai ไทย",
    "de": "German Deutsch",
    "pt": "Portuguese Português",
    "uk": "Ukrainian Українська",
    "tr": "Turkish Türkçe",
    "cs": "Czech Čeština",
    "pl": "Polish Polski",
    "ro": "Romanian Română",
    "ca": "Catalan Català",
    "nl": "Dutch Nederlands",
    "sv": "Swedish Svenska",
    "da": "Danish Dansk",
    "no": "Norwegian Norsk",
    "et": "Estonian Eesti",
    "lv": "Latvian Latviešu",
    "lt": "Lithuanian Lietuvių",
    "is": "Icelandic Íslenska",
    "ms": "Malay Bahasa Melayu",
    "sl": "Slovenian Slovenščina",
    "bg": "Bulgarian Български",
    "he": "Hebrew עברית",
    "sk": "Slovak Slovenčina",
    "hr": "Croatian Hrvatski",
    "hu": "Hungarian Magyar",
    "fa": "Persian فارسی",
    "el": "Greek Ελληνικά",
    "fil": "Filipino Filipino",
  };

  // ===========================================================================
  // RSA Encryption Keys - Backend Configuration
  // ===========================================================================

  /// Server RSA public key used to encrypt outgoing requests.
  ///
  /// The key is never committed. Supply it at build time, for example:
  ///   flutter run --dart-define=SERVER_PUBLIC_KEY="$(cat server_public.pem)"
  static const String serverPublicKey =
      String.fromEnvironment('SERVER_PUBLIC_KEY');

  /// Client RSA private key used to decrypt incoming responses.
  ///
  /// Never commit a real private key. Supply it at build time, for example:
  ///   flutter run --dart-define=CLIENT_PRIVATE_KEY="$(cat client_private.pem)"
  static const String clientPrivateKey =
      String.fromEnvironment('CLIENT_PRIVATE_KEY');

  // ===========================================================================
  // Character Sets
  // ===========================================================================

  /// Alphanumeric character set for random string generation
  /// Contains: uppercase, lowercase letters, and digits 0-9
  static const List<String> characters = [
    'a',
    'b',
    'c',
    'd',
    'e',
    'f',
    'g',
    'h',
    'i',
    'j',
    'k',
    'l',
    'm',
    'n',
    'o',
    'p',
    'q',
    'r',
    's',
    't',
    'u',
    'v',
    'w',
    'x',
    'y',
    'z',
    'A',
    'B',
    'C',
    'D',
    'E',
    'F',
    'G',
    'H',
    'I',
    'J',
    'K',
    'L',
    'M',
    'N',
    'O',
    'P',
    'Q',
    'R',
    'S',
    'T',
    'U',
    'V',
    'W',
    'X',
    'Y',
    'Z',
    '0',
    '1',
    '2',
    '3',
    '4',
    '5',
    '6',
    '7',
    '8',
    '9',
  ];

  // ===========================================================================
  // Bluetooth Encryption Key
  // ===========================================================================

  /// RSA private key for the encrypted BLE link with StackChan devices.
  ///
  /// Production devices should use a per-device key. Supply the default
  /// development key at build time, for example:
  ///   --dart-define=STACKCHAN_BLE_PRIVATE_KEY="$(cat ble_private.pem)"
  static const String stackChanBluePrivateKey =
      String.fromEnvironment('STACKCHAN_BLE_PRIVATE_KEY');
}

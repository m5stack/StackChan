/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:encrypt/encrypt.dart';
import 'package:pointycastle/asymmetric/api.dart';
import 'package:stack_chan/util/value_constant.dart';

class RsaUtil {
  /// True only when both keys used by the server/client RSA channel are present.
  static bool get isConfigured =>
      ValueConstant.serverPublicKey.trim().isNotEmpty &&
      ValueConstant.clientPrivateKey.trim().isNotEmpty;

  static bool get isStackChanBlueConfigured =>
      ValueConstant.stackChanBluePrivateKey.trim().isNotEmpty;

  static Encrypter? _encrypter;
  static Encrypter? _stackChanBlueEncrypter;

  static Encrypter get _configuredEncrypter {
    if (!isConfigured) {
      throw const FormatException('RSA keys are not configured.');
    }

    return _encrypter ??= Encrypter(
      RSA(
        publicKey:
            RSAKeyParser().parse(ValueConstant.serverPublicKey) as RSAPublicKey,
        privateKey:
            RSAKeyParser().parse(ValueConstant.clientPrivateKey)
                as RSAPrivateKey,
        encoding: RSAEncoding.OAEP,
        digest: RSADigest.SHA256,
      ),
    );
  }

  static Encrypter get _configuredStackChanBlueEncrypter {
    if (!isStackChanBlueConfigured) {
      throw const FormatException(
        'StackChan Bluetooth RSA key is not configured.',
      );
    }

    return _stackChanBlueEncrypter ??= Encrypter(
      RSA(
        privateKey:
            RSAKeyParser().parse(ValueConstant.stackChanBluePrivateKey)
                as RSAPrivateKey,
        encoding: RSAEncoding.OAEP,
        digest: RSADigest.SHA256,
      ),
    );
  }

  ///RSA Encrypt（OAEP + SHA-256）
  static String encrypt(String plainText) {
    final encrypted = _configuredEncrypter.encrypt(plainText);
    return encrypted.base64;
  }

  ///RSA Decrypt（OAEP + SHA-256）
  static String decrypt(String cipherText) {
    final encrypted = Encrypted.fromBase64(cipherText);
    return _configuredEncrypter.decrypt(encrypted);
  }

  static String decryptStackChanBlue(String cipherText) {
    final encrypted = Encrypted.fromBase64(cipherText);
    return _configuredStackChanBlueEncrypter.decrypt(encrypted);
  }
}

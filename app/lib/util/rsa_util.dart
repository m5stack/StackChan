/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:encrypt/encrypt.dart';
import 'package:pointycastle/asymmetric/api.dart';
import 'package:stack_chan/util/value_constant.dart';

class RsaUtil {
  static final _encrypter = Encrypter(
    RSA(
      publicKey:
          RSAKeyParser().parse(ValueConstant.serverPublicKey) as RSAPublicKey,
      privateKey:
          RSAKeyParser().parse(ValueConstant.clientPrivateKey) as RSAPrivateKey,
      encoding: RSAEncoding.OAEP,
      digest: RSADigest.SHA256,
    ),
  );

  ///RSA Encrypt（OAEP + SHA-256）
  static String encrypt(String plainText) {
    final encrypted = _encrypter.encrypt(plainText);
    return encrypted.base64;
  }

  ///RSA Decrypt（OAEP + SHA-256）
  static String decrypt(String cipherText) {
    final encrypted = Encrypted.fromBase64(cipherText);
    return _encrypter.decrypt(encrypted);
  }

  static String decryptStackChanBlue(String cipherText) {
    final stackChanBlueEncrypter = Encrypter(
      RSA(
        privateKey:
            RSAKeyParser().parse(ValueConstant.stackChanBluePrivateKey)
                as RSAPrivateKey,
        encoding: RSAEncoding.OAEP,
        digest: RSADigest.SHA256,
      ),
    );
    final encrypted = Encrypted.fromBase64(cipherText);
    return stackChanBlueEncrypter.decrypt(encrypted);
  }
}

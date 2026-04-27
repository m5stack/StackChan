/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:dio/dio.dart';
import 'package:flutter/cupertino.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/network/urls.dart';
import 'package:stack_chan/network/web_socket_util.dart';

import '../util/rsa_util.dart';
import '../util/value_constant.dart';

void logPrint(Object? object) {
  if (object == null) return;
  String log = object.toString();
  const int chunkSize = 800; //每次打印800字符（低于系统limit）
  //ifcontent较短,directPrint
  if (log.length <= chunkSize) {
    debugPrint(log);
    return;
  }

  //长content分段Print
  for (int i = 0; i < log.length; i += chunkSize) {
    int end = i + chunkSize;
    if (end > log.length) end = log.length;
    //usedebugPrint(比print更稳定,support更长content)
    debugPrint(log.substring(i, end));
  }
}

class Http {
  static final Http instance = Http._init();

  late final Dio _dio;

  late final BaseOptions _baseOptions;

  final List<Interceptor> _interceptors = [
    LogInterceptor(responseBody: true, logPrint: logPrint),
    InterceptorsWrapper(
      onRequest:
          (RequestOptions options, RequestInterceptorHandler handler) async {
            /// v1 mac
            final encryptedToken = RsaUtil.encrypt(
              WebSocketUtil.shared.getAuthorization(AppState.shared.deviceMac),
            );
            options.headers[ValueConstant.authorization] = encryptedToken;

            /// v2 token
            final token = await AppState.asyncPrefs.getString(
              ValueConstant.token,
            );
            if (token != null) {
              options.headers[ValueConstant.token] = token;
            }

            /// App Version
            String? version = AppState.shared.packageInfo?.version;
            if (version != null) {
              options.headers[ValueConstant.appVersion] = version;
            }

            return handler.next(options);
          },
      onResponse: (Response response, ResponseInterceptorHandler handler) {
        return handler.next(response);
      },
    ),
  ];

  Http._init() {
    _baseOptions = BaseOptions(
      baseUrl: Urls.getBaseUrl(),
      connectTimeout: const Duration(seconds: 20),
      receiveTimeout: const Duration(seconds: 20),
    );
    _dio = Dio(_baseOptions);
    _dio.interceptors.addAll(_interceptors);
  }

  Future<Response> get(
    String pathUrl, {
    dynamic data,
    Options? options,
    String? baseUrlString,
  }) async {
    if (baseUrlString != null) {
      _dio.options.baseUrl = baseUrlString;
    }
    return await _dio.get(pathUrl, queryParameters: data, options: options);
  }

  Future<Response> post(
    String pathUrl, {
    dynamic data,
    Options? options,
    String? baseUrlString,
  }) async {
    if (baseUrlString != null) {
      _dio.options.baseUrl = baseUrlString;
    }
    return await _dio.post(pathUrl, data: data, options: options);
  }

  Future<Response> put(
    String pathUrl, {
    dynamic data,
    Options? options,
    String? baseUrlString,
  }) async {
    if (baseUrlString != null) {
      _dio.options.baseUrl = baseUrlString;
    }
    return await _dio.put(pathUrl, data: data, options: options);
  }

  Future<Response> delete(
    String pathUrl, {
    dynamic data,
    Options? options,
    String? baseUrlString,
  }) async {
    if (baseUrlString != null) {
      _dio.options.baseUrl = baseUrlString;
    }
    return await _dio.delete(pathUrl, data: data, options: options);
  }

  Future<Response> postFormData(
    String pathUrl,
    FormData data, {
    Options? options,
    String? baseUrlString,
  }) async {
    if (baseUrlString != null) {
      _dio.options.baseUrl = baseUrlString;
    }
    return await _dio.post(pathUrl, data: data, options: options);
  }
}

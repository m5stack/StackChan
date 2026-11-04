/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:convert';

import 'package:dio/dio.dart';
import 'package:flutter/widgets.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:stack_chan/model/model.dart';
import 'package:stack_chan/network/http.dart';
import 'package:stack_chan/network/urls.dart';
import 'package:stack_chan/util/mac_address_validator.dart';

import '../model/XiaoZhi/XiaoZhi_model.dart';
import '../model/XiaoZhi/agent.dart';
import '../model/XiaoZhi/agent_create.dart';
import '../model/XiaoZhi/agent_template.dart';
import '../model/XiaoZhi/common_mcp_tool.dart';
import '../model/XiaoZhi/conversation.dart';
import '../model/XiaoZhi/conversation_message_data.dart';
import '../model/XiaoZhi/device.dart';
import '../model/XiaoZhi/endpoints_response.dart';
import '../model/XiaoZhi/mcp_endpoints.dart';
import '../model/XiaoZhi/pagination.dart';
import '../model/XiaoZhi/tts_list.dart';

class XiaoZhiUtil {
  static final XiaoZhiUtil shared = XiaoZhiUtil._internal();

  XiaoZhiUtil._internal() {
    _dio.options.baseUrl = "https://xiaozhi.me/";
    _dio.options.connectTimeout = const Duration(seconds: 10);
    _dio.options.receiveTimeout = const Duration(seconds: 10);
    _dio.options.validateStatus = (state) {
      return state != null && state >= 200 && state < 500;
    };
    _dio.interceptors.add(
      LogInterceptor(responseBody: true, logPrint: logPrint),
    );
    _dio.interceptors.add(
      InterceptorsWrapper(
        onRequest: (options, handler) async {
          final token = await getToken();
          if (token != null) {
            options.headers['Authorization'] = 'Bearer $token';
          }
          options.headers['Accept'] = 'application/json';
          options.headers['Content-Type'] = 'application/json';
          handler.next(options);
        },
        onResponse: (response, handler) async {
          if (response.statusCode == 401) {
            await _asyncPrefs.remove(_tokenKay);
            final newToken = await refreshXiaoZhiToken();
            if (newToken != null) {
              // Retry the request
              final Options newOptions = Options(
                method: response.requestOptions.method,
                headers: {
                  ...response.requestOptions.headers,
                  'Authorization': 'Bearer $newToken',
                  'Accept': 'application/json',
                },
              );
              try {
                final newResponse = await _dio.request(
                  response.requestOptions.path,
                  options: newOptions,
                  queryParameters: response.requestOptions.queryParameters,
                  data: response.requestOptions.data,
                );
                handler.resolve(newResponse);
                return;
              } catch (e) {
                handler.next(response);
                return;
              }
            }
          }

          // Handle session expiry responses
          if (response.data != null) {
            try {
              XiaozhiResponse xiaozhiResponse = XiaozhiResponse.fromJsonT(
                response.data,
              );

              if (!xiaozhiResponse.success &&
                  xiaozhiResponse.message == "Session expired or logged out") {
                debugPrint("XiaoZhi session expired, refreshing token");
                // Refresh the token
                final newToken = await refreshXiaoZhiToken();
                if (newToken != null) {
                  // Retry the request
                  final Options newOptions = Options(
                    method: response.requestOptions.method,
                    headers: {
                      ...response.requestOptions.headers,
                      'Authorization': 'Bearer $newToken',
                      'Accept': 'application/json',
                    },
                  );
                  try {
                    final newResponse = await _dio.request(
                      response.requestOptions.path,
                      options: newOptions,
                      queryParameters: response.requestOptions.queryParameters,
                      data: response.requestOptions.data,
                    );
                    handler.resolve(newResponse);
                    return;
                  } catch (e) {
                    debugPrint('RetryRequestFailed: $e');
                  }
                }
              }
            } catch (e) {
              debugPrint('ParseResponseFailed: $e');
            }
          }

          handler.next(response);
        },
      ),
    );
  }

  final SharedPreferencesAsync _asyncPrefs = SharedPreferencesAsync();
  final String _tokenKay = "XiaoZhiToken";
  final Dio _dio = Dio();
  static const String _xiaozhiBaseHost = "api.xiaozhi.me";

  Future<String?> getToken() async {
    String? token = await _asyncPrefs.getString(_tokenKay);
    if (token == null || token.isEmpty) {
      return await getTokenFromServer();
    }
    return token;
  }

  Future<String?> getTokenFromServer() async {
    final response = await Http.instance.get(Urls.xiaozhiToken);
    Model<String> responseData = Model.fromJsonT(response.data);
    if (responseData.isSuccess()) {
      String? token = responseData.data;
      if (token != null) {
        await _asyncPrefs.setString(_tokenKay, token);
        return token;
      }
    }
    return null;
  }

  /// Refresh the XiaoZhi access token
  Future<String?> refreshXiaoZhiToken() async {
    final response = await Http.instance.get(Urls.xiaozhiTokenRefresh);
    Model<String> responseData = Model.fromJsonT(response.data);
    if (responseData.isSuccess()) {
      String? token = responseData.data;
      if (token != null) {
        await _asyncPrefs.setString(_tokenKay, token);
        return token;
      }
    }
    return null;
  }

  /// Get the agent template list
  Future<List<AgentTemplate>> agentTemplatesList(int page, int pageSize) async {
    Map<String, dynamic> map = {"page": page, "pageSize": pageSize};
    try {
      final response = await _dio.get(
        "api/developers/agent-templates/list",
        data: map,
      );
      if (response.data != null) {
        XiaozhiResponse<ListData<AgentTemplate>> xiaozhiResponse =
            XiaozhiResponse.fromJsonT(
              response.data,
              factory: (value) => ListData.fromJson(
                value,
                (value) => AgentTemplate.fromJson(value),
              ),
            );
        if (xiaozhiResponse.success) {
          return xiaozhiResponse.data?.list ?? [];
        }
      }
    } catch (e) {
      debugPrint('LoadAgentTemplatesException: $e');
    }
    return [];
  }

  // Get device list
  Future<List<Device>> getDevice(String macAddress) async {
    final map = {'mac_address': MacAddressValidator.formatMac(macAddress)};
    try {
      final response = await _dio.get(
        "api/developers/devices",
        queryParameters: map,
      );
      if (response.data != null) {
        XiaozhiResponse<ListData<Device>> xiaozhiResponse =
            XiaozhiResponse.fromJsonT(
              response.data,
              factory: (value) =>
                  ListData.fromJson(value, (value) => Device.fromJson(value)),
            );
        if (xiaozhiResponse.success) {
          final list = xiaozhiResponse.data?.list ?? [];
          if (list.isEmpty) {
            return await getCapitalLettersMacDevice(macAddress);
          } else {
            return list;
          }
        } else {
          throw Exception('查询设备失败');
        }
      }
      return [];
    } catch (e) {
      debugPrint('查询DeviceException:$e');
      return [];
    }
  }

  Future<List<Device>> getCapitalLettersMacDevice(String macAddress) async {
    final map = {
      'mac_address': MacAddressValidator.formatLowerCaseMac(macAddress),
    };
    try {
      final response = await _dio.get(
        "api/developers/devices",
        queryParameters: map,
      );
      if (response.data != null) {
        XiaozhiResponse<ListData<Device>> xiaozhiResponse =
            XiaozhiResponse.fromJsonT(
              response.data,
              factory: (value) =>
                  ListData.fromJson(value, (value) => Device.fromJson(value)),
            );
        if (xiaozhiResponse.success) {
          return xiaozhiResponse.data?.list ?? [];
        }
      }
      return [];
    } catch (e) {
      debugPrint('查询DeviceException:$e');
      return [];
    }
  }

  /// Get voice list
  Future<TTsList?> getTtsList() async {
    final response = await _dio.get("api/user/tts-list");
    if (response.data != null) {
      XiaozhiResponse<TTsList> xiaozhiResponse = XiaozhiResponse.fromJsonT(
        response.data,
        factory: (value) => TTsList.fromJson(value),
      );
      if (xiaozhiResponse.success) {
        return xiaozhiResponse.data;
      }
    }
    return null;
  }

  /// Get model list
  Future<List<ModelData>> getModelList() async {
    final response = await _dio.get("api/roles/model-list");
    if (response.data != null) {
      XiaozhiResponse<XiaoZhiModel> xiaozhiResponse = XiaozhiResponse.fromJsonT(
        response.data,
        factory: (value) => XiaoZhiModel.fromJson(value),
      );
      if (xiaozhiResponse.success) {
        return xiaozhiResponse.data?.modelList ?? [];
      }
    }
    return [];
  }

  /// Get official MCP tools
  Future<List<CommonMcpTool>> getCommonMcpTool() async {
    try {
      final response = await _dio.get("api/agents/common-mcp-tool/list");
      if (response.data != null) {
        XiaozhiResponse<List<CommonMcpTool>> xiaozhiResponse =
            XiaozhiResponse.fromJsonT(
              response.data,
              factory: (value) => CommonMcpTool.fromListJson(value),
            );
        if (xiaozhiResponse.success) {
          return xiaozhiResponse.data ?? [];
        }
      }
    } catch (e) {
      debugPrint('LoadCommonMcpToolException: $e');
    }
    return [];
  }

  /// Create agent
  Future<int?> createAgent(AgentCreate agentParams) async {
    final response = await _dio.post("api/agents", data: agentParams.toJson());
    if (response.data != null) {
      XiaozhiResponse<dynamic> xiaozhiResponse = XiaozhiResponse.fromJsonT(
        response.data,
      );
      if (xiaozhiResponse.success) {
        int? id = xiaozhiResponse.data["id"] as int?;
        return id;
      }
    }
    return null;
  }

  /// Get agent list
  Future<List<Agent>> getAgents({
    int page = 1,
    int pageSize = 24,
    String? keyword,
  }) async {
    final Map<String, dynamic> params = {
      "page": page,
      "pageSize": pageSize,
      if (keyword != null && keyword.isNotEmpty) "keyword": keyword,
    };
    final response = await _dio.get("api/agents", queryParameters: params);
    if (response.data != null) {
      XiaozhiResponse<List<Agent>> xiaozhiResponse = XiaozhiResponse.fromJsonT(
        response.data,
        factory: (value) => Agent.fromListJson(value),
      );
      if (xiaozhiResponse.success) {
        return xiaozhiResponse.data ?? [];
      }
    }
    return [];
  }

  /// Get agent details
  Future<Agent?> getAgentDetail(int agentId) async {
    final response = await _dio.get("api/agents/$agentId");
    if (response.data != null) {
      XiaozhiResponse<Map<String, dynamic>> xiaozhiResponse =
          XiaozhiResponse.fromJsonT(response.data);
      if (xiaozhiResponse.success && xiaozhiResponse.data != null) {
        if (xiaozhiResponse.data!["agent"] != null) {
          final agent = Agent.fromJson(xiaozhiResponse.data!["agent"]);
          return agent;
        }
      }
    }
    return null;
  }

  /// Update agent
  Future<bool> updateAgent(int agentId, AgentCreate agentParams) async {
    final response = await _dio.post(
      "api/agents/$agentId/config",
      data: agentParams.toJson(),
    );
    if (response.data != null) {
      XiaozhiResponse<dynamic> xiaozhiResponse = XiaozhiResponse.fromJsonT(
        response.data,
      );
      if (xiaozhiResponse.success) {
        return true;
      }
    }
    return false;
  }

  /// Bind a device to an agent (adds the device to that agent)
  Future<bool> bindDeviceToAgent(int agentId, String verificationCode) async {
    final response = await _dio.post(
      "api/agents/$agentId/devices",
      data: {"verificationCode": verificationCode},
    );
    if (response.data != null) {
      XiaozhiResponse<dynamic> xiaozhiResponse = XiaozhiResponse.fromJsonT(
        response.data,
      );
      if (xiaozhiResponse.success) {
        return true;
      }
    }
    return false;
  }

  /// Update device alias in agent settings
  Future<bool> updateAgentDeviceAlias({
    required int agentId,
    required String macAddress,
    required String alias,
  }) async {
    final normalizedMac =
        MacAddressValidator.formatLowerCaseMac(macAddress) ??
        MacAddressValidator.formatMac(macAddress) ??
        macAddress;
    final response = await _dio.post(
      "api/agents/$agentId/devices/update-alias",
      data: {"macAddress": normalizedMac, "alias": alias},
    );
    if (response.data != null) {
      final xiaozhiResponse = XiaozhiResponse.fromJsonT(response.data);
      return xiaozhiResponse.success;
    }
    return false;
  }

  /// Enable/disable OTA auto-update for a device in agent settings
  Future<bool> updateAgentDeviceAutoUpdate({
    required int agentId,
    required String macAddress,
    required bool enabled,
  }) async {
    final normalizedMac =
        MacAddressValidator.formatLowerCaseMac(macAddress) ??
        MacAddressValidator.formatMac(macAddress) ??
        macAddress;
    final response = await _dio.post(
      "api/agents/$agentId/devices/update-auto-update",
      data: {"macAddress": normalizedMac, "autoUpdate": enabled ? 1 : 0},
    );
    if (response.data != null) {
      final xiaozhiResponse = XiaozhiResponse.fromJsonT(response.data);
      return xiaozhiResponse.success;
    }
    return false;
  }

  /// Unbind device
  Future<bool> unbindDevice(int deviceId) async {
    final response = await _dio.post(
      "api/developers/unbind-device",
      data: {"device_id": deviceId},
    );
    if (response.data != null) {
      XiaozhiResponse<dynamic> xiaozhiResponse = XiaozhiResponse.fromJsonT(
        response.data,
      );
      if (xiaozhiResponse.success) {
        return true;
      }
    }
    return false;
  }

  /// Get MCP endpoint list
  Future<List<McpEndpoints>> mcpEndpoints() async {
    final response = await _dio.get("api/developers/mcp-endpoints");
    if (response.data != null) {
      XiaozhiResponse<List<McpEndpoints>> xiaozhiResponse =
          XiaozhiResponse.fromJsonT(
            response.data,
            factory: (value) => McpEndpoints.fromListJson(value),
          );
      if (xiaozhiResponse.success) {
        return xiaozhiResponse.data ?? [];
      }
    }
    return [];
  }

  /// Create an MCP endpoint
  Future<bool> createMcpEndpoints(
    String name,
    String description,
    bool enabled,
  ) async {
    final Map<String, dynamic> map = {
      "name": name,
      "description": description,
      "enabled": enabled,
    };
    final response = await _dio.post("api/developers/mcp-endpoints", data: map);
    if (response.data != null) {
      XiaozhiResponse res = XiaozhiResponse.fromJsonT(response.data);
      return res.success;
    }
    return false;
  }

  /// Generate an MCP endpoint token for an agent
  Future<String?> generateMcpEndpointToken(int id) async {
    String url = "api/agents/$id/generate-mcp-endpoint-token";
    final response = await _dio.post(url);
    if (response.data != null) {
      XiaozhiResponse xiaozhiResponse = XiaozhiResponse.fromJsonT(
        response.data,
      );
      if (xiaozhiResponse.success) {
        return xiaozhiResponse.token;
      }
    }
    return null;
  }

  /// Generate a token for an MCP endpoint
  Future<String?> getEndpointToken(int id) async {
    String url = "api/developers/mcp-endpoints/$id/generate-endpoint-token";
    final response = await _dio.post(url);
    if (response.data != null) {
      XiaozhiResponse xiaozhiResponse = XiaozhiResponse.fromJsonT(
        response.data,
      );
      if (xiaozhiResponse.success) {
        return xiaozhiResponse.token;
      }
    }
    return null;
  }

  Future<EndpointsResponse?> endpointsList(int agentId) async {
    String url = "https://$_xiaozhiBaseHost/mcp/endpoints/list";
    final response = await _dio.get(
      url,
      queryParameters: {"endpoint_ids": "agent_$agentId"},
    );
    if (response.data != null) {
      EndpointsResponse data = EndpointsResponse.fromJson(response.data);
      return data;
    }
    return null;
  }

  /// Edit MCP endpoint information
  Future<bool> editEndpoints(
    int id, {
    String? name,
    String? description,
    bool? enabled,
  }) async {
    final Map<String, dynamic> map = {};
    if (name != null) {
      map["name"] = name;
    }
    if (description != null) {
      map["description"] = description;
    }
    if (enabled != null) {
      map["enabled"] = enabled;
    }
    String url = "api/developers/mcp-endpoints/$id";
    final response = await _dio.put(url, data: map);
    if (response.data != null) {
      XiaozhiResponse res = XiaozhiResponse.fromJsonT(response.data);
      return res.success;
    }
    return false;
  }

  /// Delete MCP endpoint
  Future<bool> deleteEndpoints(int id) async {
    String url = "api/developers/mcp-endpoints/$id";
    final response = await _dio.delete(url, data: {});
    if (response.data != null) {
      XiaozhiResponse xiaozhiResponse = XiaozhiResponse.fromJsonT(
        response.data,
      );
      if (xiaozhiResponse.success) {
        return true;
      }
    }
    return false;
  }

  Future<List<Conversation>> getConversationList({
    required String startDate,
    String? endDate,
    int? deviceId,
    int? page,
    int? pageSize,
    int? agentId,
    int? chatId,
  }) async {
    final queryParameters = <String, dynamic>{
      "startDate": startDate,
      "deviceId": deviceId,
      "page": page,
      "pageSize": pageSize,
      "agentId": agentId,
      "chatId": chatId,
    };
    if (endDate != null && endDate.isNotEmpty) {
      queryParameters["endDate"] = endDate;
    }
    queryParameters.removeWhere((key, value) => value == null);
    final response = await _dio.get(
      "api/chats/list",
      queryParameters: queryParameters,
    );
    if (response.data != null) {
      XiaozhiResponse<ListData<Conversation>> xiaozhiResponse =
          XiaozhiResponse.fromJsonT(
            response.data,
            factory: (value) => ListData.fromJson(
              value,
              (value) => Conversation.fromJson(value),
            ),
          );
      if (xiaozhiResponse.success) {
        final list = xiaozhiResponse.data?.list;
        if (list != null) {
          return list;
        }
      }
    }
    return [];
  }

  /// Delete conversation
  Future<bool> deleteConversation(int agentId, int id) async {
    String url = "api/agents/$agentId/chats/$id";
    // The XiaoZhi API requires a non-empty body when Content-Type is JSON.
    // Send an empty map to avoid a 400 "Body cannot be empty" response.
    final response = await _dio.delete(url, data: {});
    if (response.data != null) {
      XiaozhiResponse xiaozhiResponse = XiaozhiResponse.fromJsonT(
        response.data,
      );
      if (xiaozhiResponse.success) {
        return true;
      }
    }
    return false;
  }

  /// Get message list
  Future<List<ConversationMessageData>> getChatsMessages({
    required String startDate,
    String? endDate,
    int? chatId,
    int? page,
    int? pageSize,
  }) async {
    final queryParameters = <String, dynamic>{
      "startDate": startDate,
      "chatId": chatId,
      "page": page,
      "pageSize": pageSize,
    };
    if (endDate != null && endDate.isNotEmpty) {
      queryParameters["endDate"] = endDate;
    }
    queryParameters.removeWhere((key, value) => value == null);
    final response = await _dio.get(
      "api/chats/messages",
      queryParameters: queryParameters,
    );
    if (response.data != null) {
      XiaozhiResponse<ListData<ConversationMessageData>> xiaozhiResponse =
          XiaozhiResponse.fromJsonT(
            response.data,
            factory: (value) => ListData.fromJson(
              value,
              (value) => ConversationMessageData.fromJson(value),
            ),
          );
      if (xiaozhiResponse.success) {
        return xiaozhiResponse.data?.list ?? [];
      }
    }
    return [];
  }
}

class XiaozhiResponse<T> {
  bool success = false;
  T? data;
  String? message;
  Pagination? pagination;
  String? token;

  XiaozhiResponse({
    required this.success,
    this.data,
    this.message,
    this.pagination,
    this.token,
  });

  XiaozhiResponse.fromJson(
    Map<String, dynamic> map, {
    T Function(dynamic)? factory,
  }) {
    if (map["data"] != null && map["data"] != "null" && factory != null) {
      data = factory(map["data"]);
    } else {
      data = map["data"];
    }
    if (map["message"] != null && map["message"] != "null") {
      message = map["message"];
    }
    if (map["success"] != null && map["success"] != "null") {
      success = map["success"];
    }
    if (map["pagination"] != null && map["pagination"] != "null") {
      pagination = Pagination.fromJson(map["pagination"]);
    }
    if (map["token"] != null && map["token"] != "null") {
      token = map["token"];
    }
  }

  XiaozhiResponse.fromJsonT(dynamic data, {T Function(dynamic)? factory})
    : this.fromJson(data is String ? jsonDecode(data) : data, factory: factory);

  XiaozhiResponse.fromJsonString(
    String? jsonString, {
    T Function(dynamic)? factory,
  }) : this.fromJson(jsonDecode(jsonString ?? ""), factory: factory);
}

class ListData<T> {
  List<T> list;
  Pagination? pagination;

  // Use a generic list instead of a fixed Device type
  ListData({required this.list, this.pagination});

  // Generic factory: parses a list of any type
  factory ListData.fromJson(
    Map<String, dynamic> json,
    T Function(Map<String, dynamic>) fromJsonT, // Item converter
  ) {
    return ListData<T>(
      list: json['list'] != null
          ? List<T>.from(
              (json['list'] as List).map(
                (x) => fromJsonT(x as Map<String, dynamic>),
              ),
            )
          : [],
      pagination: json['pagination'] != null
          ? Pagination.fromJson(json['pagination'] as Map<String, dynamic>)
          : null,
    );
  }

  // Serialize to JSON for any generic type
  Map<String, dynamic> toJson(dynamic Function(T) toJsonT) {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['list'] = list.map((v) => toJsonT(v)).toList();
    if (pagination != null) {
      data['pagination'] = pagination!.toJson();
    }
    return data;
  }
}

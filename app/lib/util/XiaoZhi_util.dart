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
import 'package:stack_chan/util/value_constant.dart';

import '../model/XiaoZhi/License.dart';
import '../model/XiaoZhi/XiaoZhi_model.dart';
import '../model/XiaoZhi/agent.dart';
import '../model/XiaoZhi/agent_create.dart';
import '../model/XiaoZhi/agent_template.dart';
import '../model/XiaoZhi/agents_devices_activate.dart';
import '../model/XiaoZhi/common_mcp_tool.dart';
import '../model/XiaoZhi/conversation.dart';
import '../model/XiaoZhi/conversation_message_data.dart';
import '../model/XiaoZhi/device.dart';
import '../model/XiaoZhi/endpoints_response.dart';
import '../model/XiaoZhi/generateLicense.dart';
import '../model/XiaoZhi/mcp_endpoints.dart';
import '../model/XiaoZhi/pagination.dart';
import '../model/XiaoZhi/product.dart';
import '../model/XiaoZhi/tts_list.dart';

class XiaoZhiUtil {
  static final XiaoZhiUtil shared = XiaoZhiUtil._internal();

  XiaoZhiUtil._internal() {
    _dio.options.baseUrl = "https://XiaoZhi.me/";
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
            final newToken = await getTokenFromServer();
            if (newToken != null) {
              //Resend request
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

          //Handle session expiration response
          if (response.data != null) {
            try {
              XiaozhiResponse xiaozhiResponse = XiaozhiResponse.fromJsonT(
                response.data,
              );
              if (!xiaozhiResponse.success &&
                  xiaozhiResponse.message == "Session expired or logged out") {
                //Refresh token
                final newToken = await refreshXiaoZhiToken();
                if (newToken != null) {
                  //Resend request
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

  ///Refresh XiaoZhi token
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

  ///Agent template
  Future<List<AgentTemplate>> agentTemplatesList(int page, int pageSize) async {
    Map<String, dynamic> map = {"page": page, "pageSize": pageSize};
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
    return [];
  }

  ///Query device by serial number
  Future<Device?> serialNumberGetDevice(String serialNumber) async {
    final map = {'serial_number': serialNumber};
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
        //Safe access: check if list is empty first
        return list.isNotEmpty ? list.first : null;
      }
    }
    return null;
  }

  //Get device list
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

  ///Get authorization list
  Future<License?> getLicenses(String serialNumber, String productId) async {
    final map = {"query": serialNumber};
    final response = await _dio.get(
      "api/developers/products/$productId/licenses",
      queryParameters: map,
    );
    if (response.data != null) {
      XiaozhiResponse<Licenses> xiaozhiResponse = XiaozhiResponse.fromJsonT(
        response.data,
        factory: (value) => Licenses.fromJson(value),
      );
      if (xiaozhiResponse.success) {
        return xiaozhiResponse.data?.licenses.first;
      }
    }
    return null;
  }

  Future<Product?> getProductsList() async {
    final response = await _dio.get("api/developers/products/list");
    if (response.data != null) {
      XiaozhiResponse<ListData<Product>> xiaozhiResponse =
          XiaozhiResponse.fromJsonT(
            response.data,
            factory: (value) =>
                ListData.fromJson(value, (value) => Product.fromJson(value)),
          );
      if (xiaozhiResponse.success) {
        return xiaozhiResponse.data?.list.first;
      }
    }
    return null;
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

  ///Get voice list
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

  ///Get model list
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

  ///Get official MCP tools
  Future<List<CommonMcpTool>> getCommonMcpTool() async {
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
    return [];
  }

  ///Create agent
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

  ///Get agent list
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

  ///Get agent details
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

  ///Update agent
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

  ///Bind device to agent (core: add device to specified agent
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

  ///Unbind device
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

  ///Generate device authorization license
  ///[macAddress]: Device MAC address (replaces seed parameter)
  ///Returns: Authorization info (includes serial number), null on failure
  Future<GenerateLicense?> generateLicense(String macAddress) async {
    try {
      //  Get generateLicenseToken 生成License
      final generateResponse = await Http.instance.get(
        Urls.xiaozhiGenerateLicenseToken,
      );
      if (generateResponse.data == null) {
        return null;
      }
      Model<String> generateLicenseModel = Model.fromJsonT(
        generateResponse.data,
      );
      if (!generateLicenseModel.isSuccess() ||
          generateLicenseModel.data == null) {
        return null;
      }

      print("拿到generateToken");
      print(generateLicenseModel.data);

      final Map<String, dynamic> queryParams = {
        ValueConstant.token: generateLicenseModel.data,
        ValueConstant.seed: macAddress,
      };
      final response = await _dio.get(
        "api/developers/generate-license",
        queryParameters: queryParams,
      );

      if (response.data != null) {
        XiaozhiResponse<GenerateLicense> xiaozhiResponse =
            XiaozhiResponse.fromJsonT(
              response.data,
              factory: (value) => GenerateLicense.fromJson(value),
            );
        return xiaozhiResponse.data;
      }
      return null;
    } catch (e) {
      debugPrint('生成LicenseException:$e');
      return null;
    }
  }

  ///Enterprise device activation API
  Future<bool> agentsDevicesActivate(
    String serialNumber,
    String macAddress,
  ) async {
    final Map<String, dynamic> map = {
      "serial_number": serialNumber,
      "mac_address": macAddress,
    };
    final response = await _dio.post("api/agents/devices/activate", data: map);
    if (response.statusCode == 200) {
      if (response.data != null) {
        XiaozhiResponse<AgentsDevicesActivate> xiaozhiResponse =
            XiaozhiResponse.fromJsonT(
              response.data,
              factory: (value) => AgentsDevicesActivate.fromJson(value),
            );

        if (xiaozhiResponse.success) {
          //Activation successful
          return true;
        }
      }
    } else if (response.statusCode == 400) {
      ///Already added
      XiaozhiResponse xiaozhiResponse = XiaozhiResponse.fromJsonT(
        response.data,
      );
      debugPrint(xiaozhiResponse.message);
      if (xiaozhiResponse.message == "该设备已经添加过，请不要重复添加") {
        return true;
      }
    }
    return false;
  }

  ///Get MCP endpoint list
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

  ///Create mcp endpoint
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

  ///Get agent MCP endpoint
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

  ///Get endpoint token
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

  Future<EndpointsResponse?> endpointsList(int endpointIds) async {
    String url = "https://api.XiaoZhi.me/mcp/endpoints/list";
    final map = {"endpoint_ids": "agent_$endpointIds"};
    final response = await _dio.get(url, queryParameters: map);
    if (response.data != null) {
      EndpointsResponse data = EndpointsResponse.fromJson(response.data);
      return data;
    }
    return null;
  }

  ///Edit MCP endpoint information
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
    final response = await _dio.post(url, data: map);
    if (response.data != null) {
      XiaozhiResponse res = XiaozhiResponse.fromJsonT(response.data);
      return res.success;
    }
    return false;
  }

  ///Delete MCP endpoint
  Future<bool> deleteEndpoints(int id) async {
    String url = "api/developers/mcp-endpoints/$id";
    final response = await _dio.delete(url);
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

  Future<List<Conversation>> getConversationList(
    String startDate,
    int? deviceId,
    int? page,
    int? pageSize,
    int? agentId,
  ) async {
    final response = await _dio.get(
      "api/chats/list",
      queryParameters: {
        "startDate": startDate,
        "deviceId": deviceId,
        "page": page,
        "pageSize": pageSize,
        "agentId": agentId,
      },
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

  ///Delete conversation
  Future<bool> deleteConversation(int agentId, int id) async {
    String url = "api/agents/$agentId/chats/$id";
    final response = await _dio.delete(url);
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

  ///Get message list
  Future<List<ConversationMessageData>> getChatsMessages(
    Map<String, dynamic> data,
  ) async {
    final response = await _dio.get(
      "api/chats/messages",
      queryParameters: data,
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

  //Fix constructor: Use generic list instead of fixed Device type
  ListData({required this.list, this.pagination});

  //Generic factory method: support parsing any type list
  factory ListData.fromJson(
    Map<String, dynamic> json,
    T Function(Map<String, dynamic>) fromJsonT, //Type conversion function
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

  //Convert to JSON: Support any generic type serialization
  Map<String, dynamic> toJson(dynamic Function(T) toJsonT) {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['list'] = list.map((v) => toJsonT(v)).toList();
    if (pagination != null) {
      data['pagination'] = pagination!.toJson();
    }
    return data;
  }
}

class Licenses {
  final List<License> licenses;
  final Pagination? pagination;

  Licenses({required this.licenses, this.pagination});

  factory Licenses.fromJson(Map<String, dynamic> json) {
    //Core fix: Correctly call License.fromJson(x)
    final licenseList = json['licenses'] as List<dynamic>? ?? [];
    final licenses = licenseList
        .map((x) => License.fromJson(x as Map<String, dynamic>))
        .toList();

    return Licenses(
      licenses: licenses,
      pagination: json['pagination'] != null
          ? Pagination.fromJson(json['pagination'] as Map<String, dynamic>)
          : null,
    );
  }
}

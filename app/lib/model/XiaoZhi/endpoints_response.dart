/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

///responsemodel
class EndpointsResponse {
  final List<Endpoint> endpoints;

  EndpointsResponse({required this.endpoints});

  factory EndpointsResponse.fromJson(Map<String, dynamic> json) {
    return EndpointsResponse(
      endpoints:
          (json['endpoints'] as List<dynamic>?)
              ?.map((e) => Endpoint.fromJson(e as Map<String, dynamic>))
              .toList() ??
          [],
    );
  }
}

///singleendpointinfo
class Endpoint {
  final String endpointId;
  final int connectionCount;
  final String status;
  final int rpm;
  final dynamic lastRequestTime;
  final int totalRequests;
  final List<Tool> tools;
  final List<String> brokers;

  Endpoint({
    required this.endpointId,
    required this.connectionCount,
    required this.status,
    required this.rpm,
    required this.lastRequestTime,
    required this.totalRequests,
    required this.tools,
    required this.brokers,
  });

  factory Endpoint.fromJson(Map<String, dynamic> json) {
    return Endpoint(
      endpointId: json['endpointId'] ?? '',
      connectionCount: json['connectionCount'] ?? 0,
      status: json['status'] ?? '',
      rpm: json['rpm'] ?? 0,
      lastRequestTime: json['lastRequestTime'],
      totalRequests: json['totalRequests'] ?? 0,
      tools:
          (json['tools'] as List<dynamic>?)
              ?.map((e) => Tool.fromJson(e as Map<String, dynamic>))
              .toList() ??
          [],
      brokers:
          (json['brokers'] as List<dynamic>?)
              ?.map((e) => e.toString())
              .toList() ??
          [],
    );
  }
}

///toolinfo
class Tool {
  final String name;
  final String description;
  final InputSchema inputSchema;

  Tool({
    required this.name,
    required this.description,
    required this.inputSchema,
  });

  factory Tool.fromJson(Map<String, dynamic> json) {
    return Tool(
      name: json['name'] ?? '',
      description: json['description'] ?? '',
      inputSchema: InputSchema.fromJson(json['inputSchema'] ?? {}),
    );
  }
}

///input schema
class InputSchema {
  final String type;
  final Map<String, dynamic>? properties;

  InputSchema({required this.type, this.properties});

  factory InputSchema.fromJson(Map<String, dynamic> json) {
    return InputSchema(
      type: json['type'] ?? 'object',
      properties: json['properties'] != null
          ? Map<String, dynamic>.from(json['properties'])
          : null,
    );
  }
}

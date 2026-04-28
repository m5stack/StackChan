/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class Pagination {
  int? total;
  int? current;
  int? pageSize;
  bool? hasMore;
  int? page;
  int? limit;
  int? totaPages; //：fieldiserror，is totalPages

  //Constructorfunction
  Pagination({
    this.total,
    this.current,
    this.pageSize,
    this.hasMore,
    this.page,
    this.limit,
    this.totaPages,
  });

  //fromJSONparse
  factory Pagination.fromJson(Map<String, dynamic> json) {
    return Pagination(
      total: json['total'] as int?,
      current: json['current'] as int?,
      pageSize: json['pageSize'] as int?,
      hasMore: json['hasMore'] as bool?,
      page: json['page'] as int?,
      limit: json['limit'] as int?,
      totaPages:
          json['totaPages'] as int? ?? json['totalPages'] as int?, //field
    );
  }

  //convertasJSON
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    if (total != null) data['total'] = total;
    if (current != null) data['current'] = current;
    if (pageSize != null) data['pageSize'] = pageSize;
    if (hasMore != null) data['hasMore'] = hasMore;
    if (page != null) data['page'] = page;
    if (limit != null) data['limit'] = limit;
    if (totaPages != null) data['totaPages'] = totaPages;
    return data;
  }
}

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

/// API endpoint configuration for the StackChan backend server
///
/// This class contains all the base URL configurations and API endpoint paths
/// for communicating with the StackChan backend server.
///
/// Backend Configuration:
/// - Update the [url] constant to point to your backend server address
/// - The base URL is constructed as: http://<server-ip>:<port>/stackChan/
/// - WebSocket endpoint uses: ws://<server-ip>:<port>/stackChan/ws
class Urls {
  /// Backend server base address configuration
  ///
  /// IMPORTANT: Update this to your actual backend server address
  /// Format: "server-ip:port/"
  /// Example: "192.168.1.100:8080/" or "api.example.com/"
  ///
  /// For development, you can use the commented local IP below
  static const String url = "00.000.000.000:0000/";


  /// Get the HTTP base URL for API requests
  ///
  /// Returns: "http://<server-address>/stackChan/"
  static String getBaseUrl() {
    return "http://$url"
        "stackChan/";
  }

  /// Get the HTTP base URL for file operations (uploads and downloads)
  ///
  /// Returns: "http://<server-address>/"
  static String getFileUrl() {
    return "http://$url";
  }

  /// Get the WebSocket URL for real-time communication
  ///
  /// Returns: "ws://<server-address>/stackChan/ws"
  static String getWebSocketUrl() {
    return "ws://$url"
        "stackChan/ws";
  }

  // ===========================================================================
  // Device Management Endpoints
  // ===========================================================================

  /// Device registration endpoint
  /// Register a new device using its MAC address
  static const String registerMac = "api/v2/device/registerMac";

  /// Device information endpoint
  /// Retrieve device details and status
  static const String deviceInfo = "device/info";

  // ===========================================================================
  // Dance Choreography Endpoints
  // ===========================================================================

  /// Dance data endpoint (v1 legacy)
  static const String dance = "dance";

  /// Dance data endpoint (v2)
  /// Create, retrieve, update, and delete dance choreographies
  static const String v2dance = "v2/dance";

  // ===========================================================================
  // Social & Content Endpoints
  // ===========================================================================

  /// Get random list of devices for discovery
  static const String deviceRandomList = "device/randomList";

  /// File upload endpoint for media (images, videos, dance files)
  static const String uploadFile = "uploadFile";

  /// Create a new social post
  static const String postAdd = "post/add";

  /// Retrieve post details
  static const String postGet = "post/get";

  /// Delete a social post
  static const String postDelete = "post/delete";

  /// Create a comment on a post
  static const String postCommentCreate = "post/comment/create";

  /// Delete a comment from a post
  static const String postCommentDelete = "post/comment/delete";

  /// Retrieve comments for a post
  static const String postCommentGet = "post/comment/get";

  /// Panoramic image or 360 view content endpoint
  static const String pano = "pano";

  // ===========================================================================
  // Authentication & User Endpoints
  // ===========================================================================

  /// User login endpoint
  static const String login = "v2/user/login";

  /// User profile management endpoint
  static const String user = "v2/user";

  /// User devices management endpoint
  static const String devices = "v2/devices";

  /// User registration endpoint
  static const String registration = "v2/user/registration";

  // ===========================================================================
  // Device-User Binding Endpoints
  // ===========================================================================

  /// Bind a device to a user account
  static const String v2deviceBind = "v2/device/bind";

  /// Unbind a device from a user account
  static const String v2deviceUnbind = "v2/device/unbind";

  /// Restore device agent configuration
  static const String deviceAgentRestore = "v2/device/agent/restore";

  // ===========================================================================
  // XiaoZhi AI Service Endpoints
  // ===========================================================================

  /// XiaoZhi AI authentication token endpoint
  /// Retrieve token for AI service access
  static const String xiaozhiToken = "xiaozhi/token";

  /// XiaoZhi AI token refresh endpoint
  /// Refresh expired authentication tokens
  static const String xiaozhiTokenRefresh = "xiaozhi/token/refresh";

  /// Generate license token for device activation
  /// Used for StackChan device licensing and activation
  static const String xiaozhiGenerateLicenseToken = "xiaozhi/generateLicenseToken";
}
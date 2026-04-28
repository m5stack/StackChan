/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

/// Utility class for MAC address validation and formatting
///
/// Supports two common MAC address formats:
/// 1. Standard colon-separated format: 00:11:22:33:44:55
/// 2. Hyphen-separated format: 00-11-22-33-44-55
/// 3. Pure 12-character format: 001122334455
class MacAddressValidator {
  /// Validate MAC address format
  ///
  /// Supports:
  /// - Pure 12 hex characters (uppercase or lowercase)
  /// - Colon-separated format (XX:XX:XX:XX:XX:XX)
  /// - Hyphen-separated format (XX-XX-XX-XX-XX-XX)
  ///
  /// Returns true if the MAC address is valid, false otherwise
  static bool isValidMac(String? mac) {
    if (mac == null || mac.isEmpty) return false;

    // Pattern 1: With separators (colon or hyphen)
    // Matches: 00:11:22:33:44:55 or 00-11-22-33-44-55
    final macWithSeparator = RegExp(
      r'^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$',
    );

    // Pattern 2: Pure 12 hex characters (no separators)
    final macWithoutSeparator = RegExp(r'^[0-9A-Fa-f]{12}$');

    return macWithSeparator.hasMatch(mac) || macWithoutSeparator.hasMatch(mac);
  }

  /// Format MAC address to standard colon-separated uppercase format
  ///
  /// Converts any valid MAC address to: XX:XX:XX:XX:XX:XX
  /// Returns null if the input MAC is invalid
  static String? formatMac(String? mac) {
    if (mac == null || mac.isEmpty) return null;

    // Remove all separators and convert to uppercase
    String cleanMac = mac.replaceAll(RegExp(r'[:-]'), '').toUpperCase();

    // Validate before formatting
    if (cleanMac.length != 12) return null;

    // Insert colon every 2 characters
    List<String> parts = [];
    for (int i = 0; i < cleanMac.length; i += 2) {
      parts.add(cleanMac.substring(i, i + 2));
    }

    return parts.join(':');
  }

  /// Format MAC address to colon-separated lowercase format
  ///
  /// Converts any valid MAC address to: xx:xx:xx:xx:xx:xx
  /// Useful for case-sensitive comparisons or storage
  static String? formatLowerCaseMac(String? mac) {
    if (mac == null || mac.isEmpty) return null;

    // Remove all separators and convert to lowercase
    String cleanMac = mac.replaceAll(RegExp(r'[:-]'), '').toLowerCase();

    // Validate before formatting
    if (cleanMac.length != 12) return null;

    // Insert colon every 2 characters
    List<String> parts = [];
    for (int i = 0; i < cleanMac.length; i += 2) {
      parts.add(cleanMac.substring(i, i + 2));
    }

    return parts.join(':');
  }

  /// Remove all separators from MAC address
  ///
  /// Returns pure 12-character hex string in uppercase
  static String? toPureMac(String? mac) {
    if (mac == null || mac.isEmpty) return null;
    return mac.replaceAll(RegExp(r'[:-]'), '').toUpperCase();
  }

  /// Normalize MAC address for comparison
  ///
  /// Converts to lowercase without separators for consistent comparison
  static String? normalize(String? mac) {
    if (mac == null || mac.isEmpty) return null;
    return mac.replaceAll(RegExp(r'[:-]'), '').toLowerCase();
  }

  /// Check if two MAC addresses are equal (case and separator insensitive)
  static bool areEqual(String? mac1, String? mac2) {
    return normalize(mac1) == normalize(mac2);
  }
}
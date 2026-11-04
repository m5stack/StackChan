/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:convert';

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/theme_setting.dart';

import '../../app_state.dart';
import '../../util/value_constant.dart';
import '../util/popup.dart';
import '../util/scan_view.dart';

class ScanningEquipment extends StatefulWidget {
  const ScanningEquipment({super.key});

  @override
  State<StatefulWidget> createState() => _ScanningEquipmentState();
}

class _ScanningEquipmentState extends State<ScanningEquipment> {
  @override
  Widget build(BuildContext context) {
    final padding = MediaQuery.paddingOf(context);

    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      navigationBar: CupertinoNavigationBar.large(
        backgroundColor: ThemeSetting.cupertinoNavigationBarBackgroundColor(
          context,
        ),
        border: ThemeSetting.cupertinoNavigationBarBorder(context),
        padding: ThemeSetting.navigationBarPadding,
        leading: CupertinoButton(
          sizeStyle: .medium,
          foregroundColor: CupertinoTheme.of(context).primaryColor,
          onPressed: () => Navigator.maybePop(context),
          child: const Icon(CupertinoIcons.back),
        ),
        largeTitle: const Text("Scan QR Code"),
      ),
      child: Padding(
        padding: .only(
          top: 20 + padding.top,
          bottom: 20 + padding.bottom,
          left: 20 + padding.left,
          right: 20 + padding.right,
        ),
        child: ClipRSuperellipse(
          clipBehavior: .antiAliasWithSaveLayer,
          borderRadius: .circular(50),
          child: Container(
            color: CupertinoColors.black,
            width: .infinity,
            height: .infinity,
            child: ScanView(
              onDetect: (result) {
                if (result.barcodes.first.rawValue != null) {
                  readCodeString(result.barcodes.first.rawValue!);
                }
              },
            ),
          ),
        ),
      ),
    );
  }

  void readCodeString(String value) {
    try {
      final dynamic jsonData = jsonDecode(value);
      if (jsonData is Map &&
          jsonData.containsKey(ValueConstant.mac) &&
          jsonData[ValueConstant.mac] is String) {
        String mac = jsonData[ValueConstant.mac] as String;
        final RegExp macRegex = RegExp(r'[^A-F0-9]', caseSensitive: false);
        String cleanedMac = mac.toUpperCase().replaceAll(macRegex, '');
        AppState.shared.deviceMac = cleanedMac;
        AppState.shared.connectWebSocket();
        closeCustomCupertinoSheet(context);
      }
    } on FormatException catch (e) {
      debugPrint('JSONParseFailed: ${e.message}');
    } on Exception catch (e) {
      debugPrint('HandleMACAddressWhen出错: $e');
    }
  }
}

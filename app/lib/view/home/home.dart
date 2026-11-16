/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:io';
import 'dart:math' as math;

import 'package:adaptive_platform_ui/adaptive_platform_ui.dart';
import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:liquid_glass_widgets/liquid_glass_widgets.dart';
import 'package:stack_chan/util/blue_util.dart';
import 'package:stack_chan/util/value_constant.dart';
import 'package:stack_chan/view/home/settings.dart';
import 'package:stack_chan/view/home/stack_chan.dart';

import '../../app_state.dart';
import '../../model/blue_device_info.dart';
import '../app.dart';
import '../popup/user_agreement.dart';
import '../util/popup.dart';

class Home extends StatefulWidget {
  const Home({super.key});

  @override
  State<StatefulWidget> createState() => _HomeState();
}

class _HomeState extends State<Home> {
  @override
  void initState() {
    super.initState();
    init();
  }

  Future<void> init() async {
    BlueUtil.shared.blueMode = 1;

    AppState.shared.webSocketMessageMonitoring();
    if (AppState.shared.deviceMac != "") {
      AppState.shared.connectWebSocket();
    }

    // Update the device list on the main page when BLE scan results arrive
    BlueUtil.shared.blufDevicesMonitoring = (List<BlueDeviceInfo> devices) {
      AppState.shared.blueDeviceList.value = devices;
    };
    Timer.run(() {
      if (BlueUtil.shared.blueSwitch && BlueUtil.shared.automaticScanning) {
        BlueUtil.shared.startScan();
      }
    });

    /// User agreement and privacy policy
    bool? isAgree = await AppState.asyncPrefs.getBool(
      ValueConstant.agreeUserAgreement,
    );
    if (isAgree != true) {
      if (mounted) {
        showCustomCupertinoSheet(
          context: context,
          enableDrag: false,
          scrollableBuilder: (context, scrollController) {
            return const UserAgreementPopup();
          },
        );
      }
    }
  }

  int pageIndex = 0;

  @override
  void dispose() {
    BlueUtil.shared.blufDevicesMonitoring = null;
    super.dispose();
  }

  void _onTabSelected(int index) {
    setState(() {
      pageIndex = index;
    });
  }

  /// Liquid Glass is available starting with iOS 26 and macOS 26.
  static const int _liquidGlassMinOSVersion = 26;
  static final _osVersionPattern = RegExp(r'Version (\d+)');

  /// Use the native glass navigation bar only on Apple platforms whose OS supports Liquid Glass.
  bool get isHaveLiquidGlass {
    if (Platform.isIOS) {
      // Use the same check as AdaptiveScaffold so the two branches cannot diverge.
      return PlatformInfo.isIOS26OrHigher();
    }
    if (!Platform.isMacOS) return false;
    final match = _osVersionPattern.firstMatch(Platform.operatingSystemVersion);
    final majorVersion = int.tryParse(match?.group(1) ?? '') ?? 0;
    return majorVersion >= _liquidGlassMinOSVersion;
  }

  @override
  Widget build(BuildContext context) {
    if (isHaveLiquidGlass) {
      return _buildAdaptiveTabBar(context);
    }

    return _buildGlassTabBar(context);
  }

  /// Native TabBar on iOS 26+. On iPhone Duo, [AdaptiveToolbarHost] moves the
  /// tabs into a vertical side strip; nothing extra is needed here.
  Widget _buildAdaptiveTabBar(BuildContext context) {
    final mediaQueryPadding = MediaQuery.paddingOf(context);
    final orientation = MediaQuery.orientationOf(context);
    // The iOS 26 native UITabBar is a platform view and draws above Flutter routes.
    // AdaptiveScaffold does not track route changes when it has no appBar, so hide
    // the bar explicitly whenever a sheet, dialog, or new page covers Home.
    final isCurrentRoute = ModalRoute.of(context)?.isCurrent ?? true;
    // Same clearance as the glass bar, the iOS 26 native tab bar floats over
    // the page (height + home indicator).
    final verticalPadding = orientation == .portrait
        ? (mediaQueryPadding.bottom / 5 * 3)
        : mediaQueryPadding.bottom;
    final bottomSafeArea = verticalPadding + 64;
    final selectedColor = CupertinoTheme.of(context).primaryColor;
    final unselectedColor = CupertinoColors.label.resolveFrom(context);

    return AdaptiveScaffold(
      // Keep the bar visible while scrolling, like the glass bar.
      minimizeBehavior: TabBarMinimizeBehavior.never,
      // Hide the native bar while another route is on top; see the comment on
      // [isCurrentRoute] above.
      tabBarHidden: !isCurrentRoute,
      body: pageIndex == 0
          ? StackChan(bottomSafeArea: bottomSafeArea)
          : Settings(bottomSafeArea: bottomSafeArea),
      bottomNavigationBar: AdaptiveBottomNavigationBar(
        useNativeBottomBar: true,
        items: const [
          AdaptiveNavigationDestination(
            icon: "rectangle.grid.1x2",
            label: "StackChan",
          ),
          AdaptiveNavigationDestination(icon: "gear", label: "Settings"),
        ],
        selectedIndex: pageIndex,
        onTap: _onTabSelected,
        selectedItemColor: selectedColor,
        unselectedItemColor: unselectedColor,
      ),
    );
  }

  Widget _buildGlassTabBar(BuildContext context) {
    final tab = [
      const GlassTab(
        icon: Icon(CupertinoIcons.rectangle_grid_1x2),
        label: "StackChan",
      ),
      const GlassTab(icon: Icon(CupertinoIcons.settings), label: "Settings"),
    ];

    final size = MediaQuery.sizeOf(context);
    final mediaQueryPadding = MediaQuery.paddingOf(context);
    final orientation = MediaQuery.orientationOf(context);
    const double bottomBarTabWidth = 110;
    const double bottomBarHorizontalPadding = 20;
    final availableBottomBarWidth = math.max(
      0.0,
      size.width - mediaQueryPadding.left - mediaQueryPadding.right,
    );
    final bottomBarWidth = math.min(
      availableBottomBarWidth,
      bottomBarTabWidth * tab.length + bottomBarHorizontalPadding * 2,
    );

    final verticalPadding = Platform.isAndroid
        ? mediaQueryPadding.bottom
        : orientation == .portrait
        ? (mediaQueryPadding.bottom / 5 * 3)
        : mediaQueryPadding.bottom;

    final bottomSafeArea = verticalPadding + 64;

    final brightness = CupertinoTheme.brightnessOf(context);

    return Stack(
      alignment: .bottomCenter,
      children: [
        pageIndex == 0
            ? StackChan(bottomSafeArea: bottomSafeArea)
            : Settings(bottomSafeArea: bottomSafeArea),
        Padding(
          padding: .only(
            left: mediaQueryPadding.left,
            right: mediaQueryPadding.right,
          ),
          child: SizedBox(
            width: bottomBarWidth,
            child: GlassTabBar.bottom(
              quality: .premium,
              selectedIconColor: CupertinoTheme.of(context).primaryColor,
              unselectedIconColor: CupertinoColors.label.resolveFrom(context),
              horizontalPadding: bottomBarHorizontalPadding,
              settings: App.glassBottomBarThemeSettings(context),
              iconSize: 30,
              iconLabelSpacing: 2,
              verticalPadding: verticalPadding,
              tabWidth: bottomBarTabWidth,
              indicatorExpansion: const EdgeInsets.symmetric(
                horizontal: 11,
                vertical: 7,
              ),
              indicatorColor: CupertinoColors.black.withValues(
                alpha: brightness == .dark ? 0.3 : 0.1,
              ),
              interactionGlowColor: CupertinoColors.transparent,
              indicatorSettings: App.glassBottomBarThemeSettings(context)
                  .copyWith(
                    glassColor: CupertinoColors.transparent,
                    thickness: 15,
                    edgeAbsorption: 0.5,
                  ),
              tabs: tab,
              selectedIndex: pageIndex,
              onTabSelected: _onTabSelected,
            ),
          ),
        ),
      ],
    );
  }
}

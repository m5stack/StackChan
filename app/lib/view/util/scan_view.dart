/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';
import 'package:flutter_svg/svg.dart';
import 'package:mobile_scanner/mobile_scanner.dart';

class ScanView extends StatefulWidget {
  const ScanView({super.key, this.onDetect});

  final void Function(BarcodeCapture barcodes)? onDetect;

  @override
  State<StatefulWidget> createState() => _ScanViewState();
}

class _ScanViewState extends State<ScanView>
    with SingleTickerProviderStateMixin {
  final MobileScannerController controller = MobileScannerController(
    formats: [.qrCode, .ean13, .code128],
    detectionSpeed: .normal,
  );

  late AnimationController _animationController;
  late Animation<double> _scaleAnimation;

  bool isProcessing = false;

  @override
  void initState() {
    super.initState();
    _animationController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 700),
    )..repeat(reverse: true);
    _scaleAnimation = Tween<double>(begin: 0.9, end: 1.1).animate(
      CurvedAnimation(parent: _animationController, curve: Curves.easeInOut),
    );
  }

  @override
  void dispose() {
    _animationController.dispose();
    controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final double width = constraints.maxWidth;
        final double height = constraints.maxHeight;
        final double viewfinderSize = (width < height ? width : height) / 2;
        return Stack(
          alignment: .center,
          children: [
            MobileScanner(onDetect: widget.onDetect, controller: controller),
            ScaleTransition(
              scale: _scaleAnimation,
              child: SvgPicture.asset(
                "assets/viewfinder.svg",
                width: viewfinderSize,
                height: viewfinderSize,
                colorFilter: .mode(CupertinoColors.white, .srcIn),
              ),
            ),
            buildFlashlightButton(width, height, viewfinderSize),
          ],
        );
      },
    );
  }

  Widget buildFlashlightButton(
    double screenWidth,
    double screenHeight,
    double viewfinderSize,
  ) {
    bool isLandscape = screenWidth > screenHeight;
    double top, left;
    const double buttonSize = 44.0;

    if (isLandscape) {
      left =
          (screenWidth + viewfinderSize) / 2 +
          (screenWidth - (screenWidth + viewfinderSize) / 2 - buttonSize) / 2;
      top = (screenHeight - buttonSize) / 2;
    } else {
      left = (screenWidth - buttonSize) / 2;
      top =
          (screenHeight + viewfinderSize) / 2 +
          (screenHeight - (screenHeight + viewfinderSize) / 2 - buttonSize) / 2;
    }

    return Positioned(
      top: top,
      left: left,
      child: CupertinoButton(
        padding: .zero,
        child: SvgPicture.asset(
          "assets/flashlight.off.fill.svg",
          width: buttonSize,
          height: buttonSize,
          colorFilter: .mode(CupertinoColors.white, .srcIn),
        ),
        onPressed: () async {
          await controller.toggleTorch();
        },
      ),
    );
  }
}

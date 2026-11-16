/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:cupertino_ui/cupertino_ui.dart';

class GridCoordinateJoystick extends StatefulWidget {
  const GridCoordinateJoystick({
    super.key,
    required this.minX,
    required this.maxX,
    required this.minY,
    required this.maxY,
    required this.showMarking,
    required this.targetGridSize,
    required this.buttonSize,
    required this.point,
    this.onRelease,
    this.padding,
    this.onImmediatelyRelease,
  });

  final double minX;
  final double maxX;
  final double minY;
  final double maxY;
  final bool showMarking;
  final double targetGridSize;
  final double buttonSize;

  final EdgeInsetsGeometry? padding;

  final Offset point;
  final Function(Offset)? onRelease;
  final Function(Offset)? onImmediatelyRelease;

  @override
  State<StatefulWidget> createState() => _GridCoordinateJoystickState();
}

class _GridCoordinateJoystickState extends State<GridCoordinateJoystick> {
  bool _isDragging = false;
  late Offset _currentPoint;

  @override
  void initState() {
    super.initState();
    _currentPoint = widget.point;
  }

  @override
  void didUpdateWidget(covariant GridCoordinateJoystick oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.point != widget.point && !_isDragging) {
      _currentPoint = widget.point;
    }
  }

  // in _GridCoordinateJoystickState class

  void _updatePoint(Offset localPosition, Size size) {
    // Resolve the configured padding
    final padding =
        widget.padding?.resolve(Directionality.of(context)) ?? EdgeInsets.zero;

    // Size of the actual draggable area (the rect inset by padding)
    double clickableWidth = size.width - padding.left - padding.right;
    double clickableHeight = size.height - padding.top - padding.bottom;

    // Keep the input position inside the padded area
    double clampedX = localPosition.dx.clamp(
      padding.left,
      size.width - padding.right,
    );
    double clampedY = localPosition.dy.clamp(
      padding.top,
      size.height - padding.bottom,
    );

    // Map X: (clampedX - padding.left) runs from 0 to clickableWidth
    double normalizedX =
        ((clampedX - padding.left) / clickableWidth) *
            (widget.maxX - widget.minX) +
        widget.minX;

    // Same mapping for the Y axis, inverted so up is positive
    double normalizedY =
        widget.maxY -
        ((clampedY - padding.top) / clickableHeight) *
            (widget.maxY - widget.minY);

    final newPoint = Offset(normalizedX, normalizedY);

    setState(() {
      _currentPoint = newPoint;
    });

    widget.onImmediatelyRelease?.call(newPoint);
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final size = constraints.biggest;
        final padding =
            widget.padding?.resolve(Directionality.of(context)) ??
            EdgeInsets.zero;

        // Map the current value back to a screen position
        double clickableWidth = size.width - padding.left - padding.right;
        double clickableHeight = size.height - padding.top - padding.bottom;

        double xPercent =
            (_currentPoint.dx - widget.minX) / (widget.maxX - widget.minX);
        double yPercent =
            (_currentPoint.dy - widget.minY) / (widget.maxY - widget.minY);

        // Add padding.left/top as the starting offset
        double xPos = padding.left + (xPercent * clickableWidth);
        double yPos =
            padding.top + (clickableHeight - (yPercent * clickableHeight));

        return GestureDetector(
          onVerticalDragStart: (_) {},
          onHorizontalDragStart: (_) {},
          child: Listener(
            onPointerDown: (event) {
              setState(() => _isDragging = true);
              _updatePoint(event.localPosition, size);
            },
            onPointerMove: (event) {
              _updatePoint(event.localPosition, size);
            },
            onPointerUp: (event) {
              setState(() => _isDragging = false);
              widget.onRelease?.call(_currentPoint);
            },
            onPointerCancel: (_) => setState(() => _isDragging = false),
            behavior: HitTestBehavior.opaque,
            child: Stack(
              clipBehavior: Clip.none, // Let the button shadow extend past the parent bounds
              children: [
                if (widget.showMarking)
                  Positioned.fill(
                    child: CustomPaint(
                      painter: JoystickPainter(
                        point: _currentPoint,
                        minX: widget.minX,
                        maxX: widget.maxX,
                        minY: widget.minY,
                        maxY: widget.maxY,
                        // Pass padding so the painter can align its grid lines
                        padding: padding,
                        gridCountX: (clickableWidth / widget.targetGridSize)
                            .floor(),
                        gridCountY: (clickableHeight / widget.targetGridSize)
                            .floor(),
                        accentColor: CupertinoTheme.of(context).primaryColor,
                      ),
                    ),
                  ),
                Positioned(
                  // The button may sit inside the padding area or partly outside, depending on xPos/yPos
                  left: xPos - widget.buttonSize / 2,
                  top: yPos - widget.buttonSize / 2,
                  child: _buildJoystickButton(context),
                ),
              ],
            ),
          ),
        );
      },
    );
  }

  Widget _buildJoystickButton(BuildContext context) {
    return Container(
      width: widget.buttonSize,
      height: widget.buttonSize,
      decoration: BoxDecoration(
        shape: BoxShape.circle,
        color: _isDragging
            ? CupertinoTheme.of(context).primaryColor.withValues(alpha: 0.8)
            : CupertinoTheme.of(context).primaryColor,
        boxShadow: [
          BoxShadow(
            color: CupertinoColors.systemGrey.withValues(alpha: 0.2),
            blurRadius: 3,
          ),
        ],
      ),
      alignment: Alignment.center,
      child: widget.showMarking
          ? Text(
              '${_currentPoint.dx.toInt()},${_currentPoint.dy.toInt()}',
              style: TextStyle(
                color: CupertinoColors.white,
                fontSize: (widget.buttonSize * 0.25).clamp(6.0, 12.0),
              ),
            )
          : null,
    );
  }
}

class JoystickPainter extends CustomPainter {
  final Offset point;
  final double minX, maxX, minY, maxY;
  final int gridCountX, gridCountY;
  final Color accentColor;

  final EdgeInsets padding;

  JoystickPainter({
    required this.point,
    required this.minX,
    required this.maxX,
    required this.minY,
    required this.maxY,
    required this.gridCountX,
    required this.gridCountY,
    required this.accentColor,
    required this.padding,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final drawArea = Rect.fromLTWH(
      padding.left,
      padding.top,
      size.width - padding.left - padding.right,
      size.height - padding.top - padding.bottom,
    );

    final gridSpacingX = drawArea.width / gridCountX;
    final gridSpacingY = drawArea.height / gridCountY;

    final gridPaint = Paint()
      ..color = CupertinoColors.systemGrey
      ..style = PaintingStyle.stroke;

    // 1. Draw the background grid and tick labels
    for (int i = 0; i <= gridCountX; i++) {
      double x = i * gridSpacingX;
      double opacity = (i == 0 || i == gridCountX)
          ? 1.0
          : (i == gridCountX ~/ 2 ? 0.7 : 0.3);
      gridPaint.color = CupertinoColors.systemGrey.withValues(alpha: opacity);
      gridPaint.strokeWidth = opacity == 1.0 ? 2 : 1;

      canvas.drawLine(Offset(x, 0), Offset(x, size.height), gridPaint);

      // X axis labels
      double xValue = i * (maxX - minX) / gridCountX + minX;
      _drawText(
        canvas,
        xValue.toInt().toString(),
        Offset(x, size.height - 15),
        size,
      );
    }

    for (int i = 0; i <= gridCountY; i++) {
      double y = i * gridSpacingY;
      double opacity = (i == 0 || i == gridCountY)
          ? 1.0
          : (i == gridCountY ~/ 2 ? 0.7 : 0.3);
      gridPaint.color = CupertinoColors.systemGrey.withValues(alpha: opacity);
      gridPaint.strokeWidth = opacity == 1.0 ? 2 : 1;

      canvas.drawLine(Offset(0, y), Offset(size.width, y), gridPaint);

      // Y axis labels
      double yValue = maxY - i * (maxY - minY) / gridCountY;
      _drawText(canvas, yValue.toInt().toString(), Offset(10, y - 5), size);
    }

    // 2. Draw the active axis lines
    final activePaint = Paint()
      ..color = accentColor.withValues(alpha: 0.8)
      ..strokeWidth = 2
      ..style = PaintingStyle.stroke;

    double xPercent = (point.dx - minX) / (maxX - minX);
    double yPercent = (point.dy - minY) / (maxY - minY);
    double xPos = xPercent * size.width;
    double yPos = size.height - (yPercent * size.height);

    canvas.drawLine(Offset(xPos, 0), Offset(xPos, size.height), activePaint);
    canvas.drawLine(Offset(0, yPos), Offset(size.width, yPos), activePaint);
  }

  void _drawText(Canvas canvas, String text, Offset offset, Size size) {
    final tp = TextPainter(
      text: TextSpan(
        text: text,
        style: const TextStyle(color: CupertinoColors.systemGrey, fontSize: 8),
      ),
      textDirection: TextDirection.ltr,
    );
    tp.layout();
    tp.paint(canvas, offset);
  }

  @override
  bool shouldRepaint(covariant JoystickPainter oldDelegate) {
    return oldDelegate.point != point;
  }
}

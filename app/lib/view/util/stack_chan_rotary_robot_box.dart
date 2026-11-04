/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:math';
import 'dart:ui' as ui;

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:three_js/three_js.dart' as three;

import '../../model/dance_list.dart';
import '../../model/expression_data.dart';

class StackChanRotaryRobotBox extends StatelessWidget {
  final double width;
  final double height;

  const StackChanRotaryRobotBox({
    super.key,
    required this.width,
    required this.height,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: width,
      height: height,
      child: LayoutBuilder(
        builder: (context, constraints) {
          return StackChanRotaryRobotJs(
            width: constraints.maxWidth,
            height: constraints.maxHeight,
          );
        },
      ),
    );
  }
}

class StackChanRotaryRobotJs extends StatefulWidget {
  const StackChanRotaryRobotJs({
    super.key,
    required this.width,
    required this.height,
  });

  final double width;
  final double height;

  @override
  State<StackChanRotaryRobotJs> createState() => _StackChanRotaryRobotJsState();
}

class _StackChanRotaryRobotJsState extends State<StackChanRotaryRobotJs> {
  late three.ThreeJS threeJs;
  bool _threeJsDisposed = false;

  @override
  void initState() {
    super.initState();
    threeJs = three.ThreeJS(
      settings: three.Settings(
        alpha: true,
        clearAlpha: 0.0,
        clearColor: 0x000000,
      ),
      setup: setup,
      onSetupComplete: () {
        if (mounted && !_threeJsDisposed) {
          setState(() {});
        }
      },
    );
    threeJs.scene = three.Scene();
    threeJs.camera = _createCamera();
  }

  @override
  void deactivate() {
    _disposeThreeJs();
    super.deactivate();
  }

  @override
  void dispose() {
    _disposeThreeJs();
    super.dispose();
  }

  three.PerspectiveCamera _createCamera() {
    return three.PerspectiveCamera(60, _cameraAspect, 1, 300);
  }

  double get _cameraAspect {
    if (widget.width <= 0 || widget.height <= 0) {
      return 1;
    }
    return widget.width / widget.height;
  }

  void _disposeThreeJs() {
    if (_threeJsDisposed) return;
    _threeJsDisposed = true;
    threeJs.dispose();
  }

  void startAnimation(three.Object3D model) {
    if (_threeJsDisposed) return;
    threeJs.events.clear();

    threeJs.addAnimationEvent((double dt) {
      if (!mounted || _threeJsDisposed) return;
      model.rotation.z += (2 * pi / 5) * dt;
    });
  }

  Future<void> setup() async {
    if (_threeJsDisposed) return;

    // 1. Lighting setup
    final hemiLight = three.HemisphereLight(0xffffff, 0x444444, 1);
    hemiLight.position.setValues(0, 100, 0);
    threeJs.scene.add(hemiLight);

    final dirLight = three.DirectionalLight(0xffffff, 1);
    dirLight.position.setValues(50, 50, 70);
    threeJs.scene.add(dirLight);

    // 2. Camera setup
    threeJs.camera.aspect = _cameraAspect;
    threeJs.camera.position.setValues(0, -100, 0);
    threeJs.camera.lookAt(threeJs.scene.position);
    threeJs.camera.updateProjectionMatrix();

    // 3. Load the robot model
    three.GLTFLoader loader = three.GLTFLoader(flipY: true).setPath('assets/');
    final gltf = await loader.fromAsset('stack_chan_model.glb');
    if (gltf == null || !mounted || _threeJsDisposed) return;

    final model = gltf.scene;
    threeJs.scene.add(model);

    model.position.y = 15;
    model.position.z = 10;
    model.rotation.x = 20 * (pi / 180);

    addExpressionPlane();

    startAnimation(model);
  }

  void addExpressionPlane() async {
    if (_threeJsDisposed) return;
    three.Object3D? model;
    for (final child in threeJs.scene.children) {
      if (child.type == "Group") {
        model = child;
        break;
      }
    }
    if (model == null) return;
    final head = model.getObjectByName('_00_stackchan450_1');
    if (head == null) return;

    final geometry = three.PlaneGeometry(42, 32);
    three.CanvasTexture expressionTexture = three.CanvasTexture();
    final material = three.MeshBasicMaterial({
      three.MaterialProperty.map: expressionTexture,
      three.MaterialProperty.transparent: false,
      three.MaterialProperty.side: three.DoubleSide,
    });
    three.Mesh expressionPlaneMesh = three.Mesh(geometry, material);
    expressionPlaneMesh.name = "expressionPlane";
    expressionPlaneMesh.position.setValues(0, 15.8, 0);
    expressionPlaneMesh.rotation.x = -90 * pi / 180.0;
    expressionPlaneMesh.rotation.z = pi;

    const double canvasWidth = 210;
    const double canvasHeight = 160;
    final data = DanceData(
      leftEye: ExpressionItem(weight: 100),
      rightEye: ExpressionItem(weight: 100),
      mouth: ExpressionItem(weight: 0),
      yawServo: MotionDataItem(angle: 0),
      pitchServo: MotionDataItem(angle: 0),
      durationMs: 1000,
    );

    // 1. Create the canvas for drawing the expression
    final recorder = ui.PictureRecorder();
    final canvas = ui.Canvas(recorder);
    final paint = ui.Paint();

    // Background: black at 70% opacity (0xB3 = 179/255)
    paint.color = const ui.Color(0xB3000000);
    canvas.drawRect(
      const ui.Rect.fromLTWH(0, 0, canvasWidth, canvasHeight),
      paint,
    );

    const eyeSize = canvasWidth / 10;

    // Draw a single eye
    void drawEye(ExpressionItem item, ui.Offset centerOffset) {
      canvas.save();

      // Scale factor for the eye size
      final clampedSize = item.size.clamp(-100, 100);
      final sizeScale = clampedSize >= 0
          ? 1.0 + clampedSize / 100.0
          : 1.0 + clampedSize / 200.0;

      final scaledEyeSize = eyeSize * sizeScale;
      final visibleHeight = scaledEyeSize * (item.weight / 100);

      // Eye position offset
      final centerX = centerOffset.dx + item.x / 10 + eyeSize / 2;
      final centerY = centerOffset.dy + item.y / 10 + eyeSize / 2;

      final eyeRect = ui.Rect.fromCenter(
        center: ui.Offset(centerX, centerY),
        width: scaledEyeSize,
        height: scaledEyeSize,
      );

      // Rotation handle
      final rotationDegrees = item.rotation / 10.0;
      canvas.translate(centerX, centerY);
      canvas.rotate(rotationDegrees * pi / 180);
      canvas.translate(-centerX, -centerY);

      // Clip the eye to simulate how far it is open
      final clipRect = ui.Rect.fromLTRB(
        eyeRect.left,
        eyeRect.bottom - visibleHeight,
        eyeRect.right,
        eyeRect.bottom,
      );
      canvas.clipRect(clipRect);

      // Draw the white eye ellipse
      paint.color = const ui.Color(0xFFFFFFFF);
      canvas.drawOval(eyeRect, paint);

      canvas.restore();
    }

    // Base position of the eyes
    const eyeY = (canvasHeight * 0.4) - (eyeSize / 2);
    const leftEyePoint = ui.Offset((canvasWidth / 4) - (eyeSize / 2), eyeY);
    const rightEyePoint = ui.Offset(
      (canvasWidth / 4 * 3) - (eyeSize / 2),
      eyeY,
    );

    drawEye(data.leftEye, leftEyePoint);
    drawEye(data.rightEye, rightEyePoint);

    // 2. Draw the mouth
    canvas.save();

    final mouthWidth = (canvasWidth * 0.3 - data.mouth.weight / 10).toDouble();
    final mouthHeight = (3 + data.mouth.weight * 0.2).toDouble();
    final mouthX = ((canvasWidth - mouthWidth) / 2) + data.mouth.x / 10;
    final mouthY = (canvasHeight * 0.65) + data.mouth.y / 10;

    final mouthCenter = ui.Offset(
      mouthX + mouthWidth / 2,
      mouthY + mouthHeight / 2,
    );
    final mRotation = data.mouth.rotation / 10.0;

    canvas.translate(mouthCenter.dx, mouthCenter.dy);
    canvas.rotate(mRotation * pi / 180);
    canvas.translate(-mouthCenter.dx, -mouthCenter.dy);

    final mouthRect = ui.Rect.fromLTWH(mouthX, mouthY, mouthWidth, mouthHeight);
    paint.color = const ui.Color(0xFFFFFFFF);

    canvas.drawRRect(
      ui.RRect.fromRectAndRadius(
        mouthRect,
        ui.Radius.circular(mouthHeight / 2),
      ),
      paint,
    );
    canvas.restore();

    // 3. Convert the canvas into an expression texture
    final picture = recorder.endRecording();
    final image = await picture.toImage(
      canvasWidth.toInt(),
      canvasHeight.toInt(),
    );

    if (!mounted || _threeJsDisposed) {
      image.dispose();
      return;
    }

    // Use raw RGBA rather than PNG for the texture upload
    final byteData = await image.toByteData(format: ui.ImageByteFormat.rawRgba);

    if (byteData != null) {
      if (!mounted || _threeJsDisposed) {
        image.dispose();
        return;
      }
      // Convert to a three_js Uint8Array
      final uint8List = byteData.buffer.asUint8List();
      // final nativeArray = three.Uint8Array.fromList(uint8List);

      // Update the texture
      expressionTexture.image = three.ImageElement(
        data: uint8List,
        width: canvasWidth.toInt(),
        height: canvasHeight.toInt(),
      );

      // Mark the texture and its source as needing an update
      expressionTexture.needsUpdate = true;

      // Force MeshBasicMaterial to re-read the texture as well
      if (expressionPlaneMesh.material is three.Material) {
        (expressionPlaneMesh.material as three.Material).needsUpdate = true;
      }
    }

    if (!mounted || _threeJsDisposed) {
      image.dispose();
      return;
    }
    head.add(expressionPlaneMesh);
    material.needsUpdate = true;

    image.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return threeJs.build();
  }
}

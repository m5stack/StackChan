/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:math';
import 'dart:ui' as ui;

import 'package:flutter/cupertino.dart';
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
        setState(() {});
      },
    );
  }

  @override
  void dispose() {
    threeJs.dispose();
    super.dispose();
  }

  void startAnimation(three.Object3D model) {
    threeJs.events.clear();

    threeJs.addAnimationEvent((double dt) {
      if (!mounted) return;
      model.rotation.z += (2 * pi / 5) * dt;
    });
  }

  Future<void> setup() async {
    threeJs.scene = three.Scene();

    //1. set (Maintain / KeepNot)
    final hemiLight = three.HemisphereLight(0xffffff, 0x444444, 1);
    hemiLight.position.setValues(0, 100, 0);
    threeJs.scene.add(hemiLight);

    final dirLight = three.DirectionalLight(0xffffff, 1);
    dirLight.position.setValues(50, 50, 70);
    threeJs.scene.add(dirLight);

    //2. set
    threeJs.camera = three.PerspectiveCamera(
      60,
      widget.width / widget.height,
      1,
      300,
    );
    threeJs.camera.position.setValues(0, -100, 0);
    threeJs.camera.lookAt(threeJs.scene.position);

    //3. loadmodel
    three.GLTFLoader loader = three.GLTFLoader(flipY: true).setPath('assets/');
    final gltf = await loader.fromAsset('stack_chan_model.glb');
    if (gltf == null || !mounted) return;

    final model = gltf.scene;
    threeJs.scene.add(model);

    model.position.y = 15;
    model.position.z = 10;
    model.rotation.x = 20 * (pi / 180);

    addExpressionPlane();

    startAnimation(model);
  }

  void addExpressionPlane() async {
    final model = threeJs.scene.children.firstWhere(
      (element) => element.type == "Group",
    );
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

    final double canvasWidth = 210;
    final double canvasHeight = 160;
    final data = DanceData(
      leftEye: ExpressionItem(weight: 100),
      rightEye: ExpressionItem(weight: 100),
      mouth: ExpressionItem(weight: 0),
      yawServo: MotionDataItem(angle: 0),
      pitchServo: MotionDataItem(angle: 0),
      durationMs: 1000,
    );

    //1. createdrawExpressioncanvas
    final recorder = ui.PictureRecorder();
    final canvas = ui.Canvas(recorder);
    final paint = ui.Paint();

    //background:With / Carry 70% transparency (0xB3 = 179/255)
    paint.color = const ui.Color(0xB3000000);
    canvas.drawRect(ui.Rect.fromLTWH(0, 0, canvasWidth, canvasHeight), paint);

    final eyeSize = canvasWidth / 10;

    //draweyefunction
    void drawEye(ExpressionItem item, ui.Offset centerOffset) {
      canvas.save();

      //calculatesizescale
      final clampedSize = item.size.clamp(-100, 100);
      final sizeScale = clampedSize >= 0
          ? 1.0 + clampedSize / 100.0
          : 1.0 + clampedSize / 200.0;

      final scaledEyeSize = eyeSize * sizeScale;
      final visibleHeight = scaledEyeSize * (item.weight / 100);

      //positionoffset
      final centerX = centerOffset.dx + item.x / 10 + eyeSize / 2;
      final centerY = centerOffset.dy + item.y / 10 + eyeSize / 2;

      final eyeRect = ui.Rect.fromCenter(
        center: ui.Offset(centerX, centerY),
        width: scaledEyeSize,
        height: scaledEyeSize,
      );

      //rotatehandle
      final rotationDegrees = item.rotation / 10.0;
      canvas.translate(centerX, centerY);
      canvas.rotate(rotationDegrees * pi / 180);
      canvas.translate(-centerX, -centerY);

      //createcropeye
      final clipRect = ui.Rect.fromLTRB(
        eyeRect.left,
        eyeRect.bottom - visibleHeight,
        eyeRect.right,
        eyeRect.bottom,
      );
      canvas.clipRect(clipRect);

      //draweye
      paint.color = const ui.Color(0xFFFFFFFF);
      canvas.drawOval(eyeRect, paint);

      canvas.restore();
    }

    //calculateeyeposition
    final eyeY = (canvasHeight * 0.4) - (eyeSize / 2);
    final leftEyePoint = ui.Offset((canvasWidth / 4) - (eyeSize / 2), eyeY);
    final rightEyePoint = ui.Offset(
      (canvasWidth / 4 * 3) - (eyeSize / 2),
      eyeY,
    );

    drawEye(data.leftEye, leftEyePoint);
    drawEye(data.rightEye, rightEyePoint);

    //2. drawmouth
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

    //3. will Canvas convertastexturedata
    final picture = recorder.endRecording();
    final image = await picture.toImage(
      canvasWidth.toInt(),
      canvasHeight.toInt(),
    );

    if (!mounted) {
      image.dispose();
      return;
    }

    //[Core Fix]:use rawRgba And / WhileNotis png
    final byteData = await image.toByteData(format: ui.ImageByteFormat.rawRgba);

    if (byteData != null) {
      //convertas three_js Uint8Array
      final uint8List = byteData.buffer.asUint8List();
      final nativeArray = three.Uint8Array.fromList(uint8List);

      //updatetexture
      expressionTexture.image = three.ImageElement(
        data: nativeArray,
        width: canvasWidth.toInt(),
        height: canvasHeight.toInt(),
      );

      //marktextureNeedupdate
      expressionTexture.needsUpdate = true;

      //ifuse MeshBasicMaterial,EnsureWillrereadtexture
      if (expressionPlaneMesh.material is three.Material) {
        (expressionPlaneMesh.material as three.Material).needsUpdate = true;
      }
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

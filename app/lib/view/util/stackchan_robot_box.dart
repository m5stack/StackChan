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

class StackChanRobotBox extends StatelessWidget {
  final DanceData data;
  final double width;
  final double height;
  final bool topLook;
  final bool allowsCameraControl;
  final bool mirrorFace;

  const StackChanRobotBox({
    super.key,
    required this.width,
    required this.height,
    required this.data,
    this.topLook = false,
    this.allowsCameraControl = false,
    this.mirrorFace = false,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: width,
      height: height,
      child: LayoutBuilder(
        builder: (context, constraints) {
          return StackchanRobotJs(
            width: constraints.maxWidth,
            height: constraints.maxHeight,
            data: data,
            topLook: topLook,
            allowsCameraControl: allowsCameraControl,
            mirrorFace: mirrorFace,
          );
        },
      ),
    );
  }
}

class StackchanRobotJs extends StatefulWidget {
  const StackchanRobotJs({
    super.key,
    required this.data,
    required this.width,
    required this.height,
    required this.topLook,
    required this.allowsCameraControl,
    required this.mirrorFace,
  });

  final DanceData data;
  final double width;
  final double height;
  final bool topLook;
  final bool allowsCameraControl;
  final bool mirrorFace;

  @override
  State<StatefulWidget> createState() => _StackchanRobotThreeState();
}

class _StackchanRobotThreeState extends State<StackchanRobotJs> {
  late three.ThreeJS threeJs;

  @override
  void initState() {
    super.initState();
    threeJs = three.ThreeJS(
      settings: three.Settings(
        alpha: true,
        clearAlpha: 0.0,
        clearColor: 0x000000,
        antialias: true,
        toneMapping: three.ReinhardToneMapping,
        toneMappingExposure: 1,
      ),
      onSetupComplete: () {
        setState(() {});
      },
      setup: setup,
    );
  }

  @override
  void dispose() {
    threeJs.dispose();
    super.dispose();
  }

  @override
  void didUpdateWidget(covariant StackchanRobotJs oldWidget) {
    super.didUpdateWidget(oldWidget);
    applyDanceData();
    if (oldWidget.topLook != widget.topLook) {
      setupCamera();
    }
  }

  Future<void> setup() async {
    threeJs.scene = three.Scene();

    //translated comment
    final hemiLight = three.HemisphereLight(0xffffff, 0x444444, 1);
    hemiLight.position.setValues(0, 100, 0);
    threeJs.scene.add(hemiLight);

    //translated comment
    final dirLight = three.DirectionalLight(0xffffff, 1);
    dirLight.position.setValues(50, 50, 70);
    threeJs.scene.add(dirLight);

    //andset
    threeJs.camera = three.PerspectiveCamera(
      60,
      widget.width / widget.height,
      1,
      300,
    );
    threeJs.camera.position.setValues(0, -100, 0);

    //loadmodel
    three.GLTFLoader loader = three.GLTFLoader(flipY: true).setPath('assets/');
    final sky = await loader.fromAsset('stack_chan_model.glb');

    if (sky == null || !mounted) return;

    final model = sky.scene;

    threeJs.scene.add(model);

    setupCamera();

    setupRobotHierarchy();

    applyDanceData();
  }

  //setAngle
  void setupCamera() {
    if (widget.topLook) {
      threeJs.camera.position.setValues(0, -100, 70);
    } else {
      threeJs.camera.position.setValues(0, -100, 0);
    }
    threeJs.camera.lookAt(threeJs.scene.position);
  }

  three.Object3D yawAxis = three.Object3D();
  three.Object3D pitchAxis = three.Object3D();

  three.Mesh? expressionPlaneMesh; //faceshow
  three.CanvasTexture? expressionTexture; //facecanvastexture
  final double canvasWidth = 210; //canvas（correspondingiOS 42*5）
  final double canvasHeight = 160; //canvas（correspondingiOS 32*5）
  final String expressionPlaneName = "expressionPlane"; //name（foriOS）
  Function(double)? currentRotationEvent;

  //translated comment
  void setupRobotHierarchy() {
    final model = threeJs.scene.children.firstWhere(
      (element) => element.type == "Group",
    );
    final foundation = model.getObjectByName('_00_stackchan450_3');
    final centralComponent = model.getObjectByName('_00_stackchan450_2');
    final head = model.getObjectByName('_00_stackchan450_1');

    if (foundation == null || centralComponent == null || head == null) return;

    //========== LeftRightto(yaw axis)logic(originalhaslogicCankeep,) ==========
    final centralWorldPos = centralComponent.worldPosition();
    centralWorldPos.y -= 20;
    yawAxis.setWorldPosition(centralWorldPos);
    foundation.add(yawAxis);
    final centralWorldTransform = centralComponent.worldTransform();
    final centralWorldPosition = centralComponent.worldPosition();
    yawAxis.add(centralComponent);
    centralComponent.setWorldTransform(centralWorldTransform);
    centralComponent.setWorldPosition(centralWorldPosition);

    //========== UpDown(pitch axis)logic(corefixPart) ==========
    final headWorldPosition = head.worldPosition();
    final headWorldTransform = head.worldTransform();
    final pitchAxisWorldPosition = pitchAxis.worldPosition();
    pitchAxisWorldPosition.y -= 25;
    pitchAxis.setWorldPosition(pitchAxisWorldPosition);
    centralComponent.add(pitchAxis);
    pitchAxis.add(head);
    head.setWorldTransform(headWorldTransform);
    head.setWorldPosition(headWorldPosition);

    addExpressionPlane();
  }

  void addExpressionPlane() {
    final model = threeJs.scene.children.firstWhere(
      (element) => element.type == "Group",
    );
    final head = model.getObjectByName('_00_stackchan450_1');
    if (head == null) return;

    final geometry = three.PlaneGeometry(42, 32);
    expressionTexture = three.CanvasTexture();
    final material = three.MeshBasicMaterial({
      three.MaterialProperty.map: expressionTexture,
      three.MaterialProperty.transparent: false,
      three.MaterialProperty.side: three.DoubleSide,
    });
    expressionPlaneMesh = three.Mesh(geometry, material);
    expressionPlaneMesh!.name = "expressionPlane";
    expressionPlaneMesh!.position.setValues(0, 15.8, 0);
    expressionPlaneMesh!.rotation.x = -90 * pi / 180.0;
    expressionPlaneMesh!.rotation.z = pi;
    head.add(expressionPlaneMesh);
    material.needsUpdate = true;
  }

  //writedata
  void applyDanceData() {
    updateServos();
    updateExpression();
    updateRGBColor();
    setupContinuousRotation();
  }

  void updateServos() async {
    final data = widget.data;
    if (data.yawServo.rotate == 0) {
      double clampedYaw = data.yawServo.angle / 10.0;
      if (clampedYaw < -128) clampedYaw = -128;
      if (clampedYaw > 128) clampedYaw = 128;
      yawAxis.rotation.z = clampedYaw * pi / 180.0;
    }
    double clampedPitch = data.pitchServo.angle / 10.0;
    if (clampedPitch < 0) clampedPitch = 0;
    if (clampedPitch > 90) clampedPitch = 90;
    pitchAxis.rotation.x = -clampedPitch * pi / 180.0;
  }

  void setupContinuousRotation() {
    final data = widget.data;

    if (currentRotationEvent != null) {
      threeJs.events.remove(currentRotationEvent);
      currentRotationEvent = null;
    }

    if (data.yawServo.rotate != 0) {
      double rotateSpeed = data.yawServo.rotate / 10.0;
      double radiansPerSecond = rotateSpeed * pi / 180.0;
      currentRotationEvent = (double dt) {
        yawAxis.rotation.z -= radiansPerSecond * dt;
      };
      threeJs.addAnimationEvent(currentRotationEvent!);
    }
  }

  void updateRGBColor() {
    final threeColor = toThreeColor(widget.data.leftRgbColor);
    for (var node in threeJs.scene.children) {
      if (node is three.Mesh) {
        if (node.material != null) {
          if (node.material!.name == "MTL12") {
            if (node.material! is three.MeshStandardMaterial) {
              node.material!.emissive = threeColor;
            } else {
              node.material!.color = threeColor;
            }
          }
        }
      }
    }
  }

  Future<void> updateExpression() async {
    final data = widget.data;
    if (expressionPlaneMesh == null || expressionTexture == null) {
      return;
    }

    //1. createdrawExpressioncanvas
    final recorder = ui.PictureRecorder();
    final canvas = ui.Canvas(recorder);
    final paint = ui.Paint();

    //fliphandle(Yaxisflip)
    if (widget.mirrorFace) {
      canvas.save();
      canvas.translate(canvasWidth, 0);
      canvas.scale(-1, 1);
    }

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

    //resumecanvasstate（ifperformflip）
    if (widget.mirrorFace) {
      canvas.restore();
    }

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
      expressionTexture!.image = three.ImageElement(
        data: nativeArray,
        width: canvasWidth.toInt(),
        height: canvasHeight.toInt(),
      );

      //marktextureNeedupdate
      expressionTexture!.needsUpdate = true;

      //ifuse MeshBasicMaterial,EnsureWillrereadtexture
      if (expressionPlaneMesh!.material is three.Material) {
        (expressionPlaneMesh!.material as three.Material).needsUpdate = true;
      }
    }
    image.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return threeJs.build();
  }

  three.Color toThreeColor(String rgbString) {
    String hex = rgbString.replaceFirst('#', '');
    if (hex.length == 6) {
      hex = 'FF$hex';
    } else if (hex.length != 8) {
      return three.Color(1, 1, 1);
    }
    final intValue = int.parse(hex, radix: 16);
    final int a = (intValue >> 24) & 0xFF;
    final int r = (intValue >> 16) & 0xFF;
    final int g = (intValue >> 8) & 0xFF;
    final int b = intValue & 0xFF;
    return three.Color(r / 255.0, g / 255.0, b / 255.0);
  }
}

extension Object3DUtil on three.Object3D {
  three.Vector3 worldPosition() {
    final position = three.Vector3.zero();
    getWorldPosition(position);
    return position;
  }

  void setWorldPosition(three.Vector3 worldPosition) {
    if (parent != null) {
      parent!.updateWorldMatrix(true, false);
      final inverseParentMatrix = three.Matrix4()
          .setFrom(parent!.matrixWorld)
          .invert();
      final localPosition = worldPosition.clone().applyMatrix4(
        inverseParentMatrix,
      );
      position.setFrom(localPosition);
    } else {
      position.setFrom(worldPosition);
    }
  }

  three.Quaternion worldTransform() {
    final worldQuaternion = three.Quaternion();
    getWorldQuaternion(worldQuaternion);
    return worldQuaternion;
  }

  void setWorldTransform(three.Quaternion worldTransform) {
    if (parent != null) {
      parent!.updateWorldMatrix(true, false);
      final parentWorldQuaternion = three.Quaternion();
      parent!.getWorldQuaternion(parentWorldQuaternion);

      final inverseParentQuaternion = parentWorldQuaternion.clone().conjugate();
      quaternion.setFrom(inverseParentQuaternion.multiply(worldTransform));
    } else {
      quaternion.setFrom(worldTransform);
    }
  }
}

/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/cupertino.dart';
import 'package:flutter_slidable/flutter_slidable.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:get/get.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/model/dance_list.dart';
import 'package:stack_chan/model/model.dart';
import 'package:stack_chan/network/http.dart';
import 'package:stack_chan/network/urls.dart';
import 'package:stack_chan/util/blue_util.dart';
import 'package:stack_chan/util/extension.dart';
import 'package:stack_chan/util/music_util.dart';
import 'package:stack_chan/util/value_constant.dart';
import 'package:stack_chan/view/home/dance.dart';
import 'package:stack_chan/view/home/record_dance.dart';

class DanceListPage extends StatefulWidget {
  const DanceListPage({super.key});

  @override
  State<StatefulWidget> createState() => _DanceListPageState();
}

class DanceListPageModel extends GetxController {
  RxList<DanceList> list = RxList([]);
  RxInt runId = RxInt(-1);
  RxBool isLoopMode = RxBool(false);
  RxBool isConnectBlue = RxBool(false);
}

class _DanceListPageState extends State<DanceListPage> {
  DanceListPageModel model = DanceListPageModel();
  bool isPlaying = false;
  Timer? _playTimer;
  final List<Future<void>?> _bluetoothPlayTasks = [];

  @override
  void initState() {
    super.initState();
    BlueUtil.shared.connectionStateChanged = (device, status) {
      model.isConnectBlue.value = status;
    };
    if (BlueUtil.shared.currentPeripheral == null) {
      model.isConnectBlue.value = false;
    } else {
      model.isConnectBlue.value = true;
    }
    if (AppState.shared.deviceControlMode == 1) {
      BlueUtil.shared.blueMode = 2;
    }
    getDanceList();
  }

  @override
  void dispose() {
    model.onClose();
    stopPlay();
    if (AppState.shared.deviceControlMode == 1) {
      BlueUtil.shared.blueMode = 1;
    }
    BlueUtil.shared.connectionStateChanged = null;
    if (mounted) {
      FocusScope.of(context).unfocus();
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final primaryColor = CupertinoTheme.of(context).primaryColor;
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemBackground.resolveFrom(context),
      child: Obx(
        () => CustomScrollView(
          slivers: [
            CupertinoSliverNavigationBar(
              largeTitle: Text("Dance List"),
              trailing: Row(
                mainAxisSize: .min,
                children: [
                  CupertinoButton(
                    padding: const EdgeInsets.all(8),
                    borderRadius: BorderRadius.circular(20),
                    child: Obx(
                      () => SvgPicture.asset(
                        model.isLoopMode.value
                            ? "assets/repeat.svg"
                            : "assets/repeat.1.svg",
                        colorFilter: ColorFilter.mode(
                          primaryColor,
                          BlendMode.srcIn,
                        ),
                        width: 24,
                        height: 24,
                      ),
                    ),
                    onPressed: () {
                      model.isLoopMode.value = !model.isLoopMode.value;
                    },
                  ),
                  const SizedBox(width: 4),
                  //Controlmodeswitch - optimizestyle
                  CupertinoButton(
                    padding: const EdgeInsets.all(10),
                    minimumSize: .zero,
                    borderRadius: BorderRadius.circular(16),
                    color: primaryColor.withValues(alpha: 0.1),
                    child: Obx(() {
                      if (AppState.shared.deviceControlMode == 0) {
                        return Text(
                          "Network",
                          style: TextStyle(
                            color: primaryColor,
                            fontSize: 14,
                            fontWeight: FontWeight.w600,
                          ),
                        );
                      } else {
                        return Row(
                          mainAxisSize: .min,
                          spacing: 5,
                          children: [
                            Text(
                              "Bluetooth",
                              style: TextStyle(
                                color: primaryColor,
                                fontSize: 14,
                                fontWeight: FontWeight.w600,
                              ),
                            ),
                            Icon(
                              CupertinoIcons.circle_filled,
                              size: 12,
                              color: model.isConnectBlue.value
                                  ? CupertinoColors.activeGreen
                                  : CupertinoColors.destructiveRed,
                            ),
                          ],
                        );
                      }
                    }),
                    onPressed: () {
                      if (AppState.shared.deviceControlMode == 0) {
                        AppState.shared.deviceControlMode = 1;
                        BlueUtil.shared.blueMode = 2;
                      } else {
                        AppState.shared.deviceControlMode = 0;
                        BlueUtil.shared.blueMode = 1;
                      }
                    },
                  ),
                ],
              ),
            ),
            CupertinoSliverRefreshControl(onRefresh: getDanceList),
            SliverPadding(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 20),
              sliver: SliverList.separated(
                itemCount: model.list.length + 1,
                itemBuilder: listItem,
                separatorBuilder: (context, index) =>
                    const SizedBox(height: 16),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Future<void> showEditDanceName(
    List<DanceData> danceList,
    String musicUrl,
    String? musicName,
  ) async {
    String text = musicName ?? "";
    String errorMessage = "";
    TextEditingController controller = TextEditingController(text: text);

    await showCupertinoDialog(
      context: context,
      builder: (context) {
        return StatefulBuilder(
          builder: (context, setDialogState) {
            return CupertinoAlertDialog(
              title: const Text("Please give the dance a name"),
              content: Padding(
                padding: const EdgeInsets.only(bottom: 10),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    CupertinoTextField(
                      controller: controller,
                      maxLength: 25,
                      decoration: BoxDecoration(
                        border: Border.all(
                          color: errorMessage.isNotEmpty
                              ? CupertinoColors.destructiveRed
                              : CupertinoColors.separator.resolveFrom(context),
                          width: 0.5,
                        ),
                        borderRadius: BorderRadius.circular(8),
                      ),
                      placeholder: "Enter dance name",
                      onChanged: (value) {
                        text = value;
                        if (errorMessage.isNotEmpty) {
                          setDialogState(() {
                            errorMessage = "";
                          });
                        }
                      },
                      autofocus: true,
                    ),
                    if (errorMessage.isNotEmpty)
                      Padding(
                        padding: const EdgeInsets.only(top: 8),
                        child: Text(
                          errorMessage,
                          style: const TextStyle(
                            color: CupertinoColors.destructiveRed,
                            fontSize: 12,
                          ),
                        ),
                      ),
                  ],
                ),
              ),
              actions: [
                CupertinoDialogAction(
                  child: const Text("Cancel"),
                  onPressed: () => Navigator.of(context).pop(),
                ),
                CupertinoDialogAction(
                  child: const Text("Confirm"),
                  onPressed: () {
                    if (text.isEmpty) {
                      setDialogState(() {
                        errorMessage = "Please enter the name of the dance";
                      });
                      return;
                    }

                    //checknamewhetherAlreadyexist
                    bool nameExists = model.list.any(
                      (item) => item.danceName == text,
                    );

                    if (nameExists) {
                      setDialogState(() {
                        errorMessage = "This dance name already exists";
                      });
                      return;
                    }

                    Navigator.of(context).pop();
                    addDance(danceList, musicUrl, text);
                  },
                ),
              ],
            );
          },
        );
      },
    );
  }

  Widget listItem(BuildContext context, int index) {
    final double itemHeight = 110;
    final double itemRadius = 20;
    final primaryColor = CupertinoTheme.of(context).primaryColor;
    final textColor = CupertinoColors.label.resolveFrom(context);
    final subTextColor = CupertinoColors.secondaryLabel.resolveFrom(context);

    //最after1个addbutton - optimizestyle
    if (index == model.list.length) {
      return Container(
        height: itemHeight,
        decoration: BoxDecoration(
          color: CupertinoColors.secondarySystemBackground.resolveFrom(context),
          borderRadius: BorderRadius.circular(itemRadius),
          boxShadow: [
            BoxShadow(
              color: CupertinoColors.black.withValues(alpha: 0.04),
              blurRadius: 8,
              offset: const Offset(0, 2),
            ),
          ],
        ),
        child: CupertinoButton(
          padding: EdgeInsets.zero,
          borderRadius: BorderRadius.circular(itemRadius),
          onPressed: () {
            stopPlay();
            showCupertinoSheet(
              context: context,
              useNestedNavigation: true,
              builder: (context) {
                return RecordDance(
                  onResult: (danceList, musicUrl, musicName) async {
                    //Wait 2 Second(s),然afterexecute
                    showEditDanceName(danceList, musicUrl, musicName);

                    // if (musicName != null &&
                    //     !model.list.any(
                    //       (item) => item.danceName == musicName,
                    //     )) {
                    //   addDance(danceList, musicUrl, musicName);
                    // } else {
                    //   showCupertinoDialog(
                    //     context: context,
                    //     builder: (context) {
                    //       String text = "";
                    //       return CupertinoAlertDialog(
                    //         title: const Text("Please give the dance a name"),
                    //         content: Padding(
                    //           padding: const EdgeInsets.only(bottom: 10),
                    //           child: CupertinoTextField(
                    //             maxLength: 25,
                    //             decoration: BoxDecoration(
                    //               border: Border.all(
                    //                 color: CupertinoColors.separator
                    //                     .resolveFrom(context),
                    //                 width: 0.5,
                    //               ),
                    //               borderRadius: BorderRadius.circular(8),
                    //             ),
                    //             onChanged: (value) {
                    //               text = value;
                    //             },
                    //           ),
                    //         ),
                    //         actions: [
                    //           CupertinoDialogAction(
                    //             child: const Text("Cancel"),
                    //             onPressed: () => Navigator.of(context).pop(),
                    //           ),
                    //           CupertinoDialogAction(
                    //             child: const Text("Confirm"),
                    //             onPressed: () {
                    //               if (text.isEmpty) {
                    //                 AppState.shared.showToast(
                    //                   "Please enter the name of the dance",
                    //                 );
                    //                 return;
                    //               }
                    //               Navigator.of(context).pop();
                    //               addDance(danceList, musicUrl, text);
                    //             },
                    //           ),
                    //         ],
                    //       );
                    //     },
                    //   );
                    // }
                  },
                );
              },
            );
          },
          child: Center(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                SvgPicture.asset(
                  "assets/plus.circle.svg",
                  colorFilter: ColorFilter.mode(primaryColor, BlendMode.srcIn),
                  width: 36,
                  height: 36,
                ),
                const SizedBox(height: 8),
                Text(
                  "Add New Dance",
                  style: TextStyle(
                    color: primaryColor,
                    fontSize: 14,
                    fontWeight: FontWeight.w600,
                  ),
                ),
              ],
            ),
          ),
        ),
      );
    } else {
      DanceList dance = model.list[index];
      //useSlidableImplementLeft滑menu

      return Slidable(
        key: Key('dance_${dance.id}'),
        //Left滑config
        endActionPane: ActionPane(
          extentRatio: 0.6,
          motion: const ScrollMotion(), //滑动动效
          dismissible: DismissiblePane(
            onDismissed: () => deleteDance(dance.id),
          ),
          children: [
            //Editbutton
            SlidableAction(
              onPressed: (_) => Navigator.of(context).push(
                CupertinoPageRoute(
                  builder: (context) => Dance(danceInfo: dance),
                ),
              ),
              backgroundColor: CupertinoColors.systemBlue.withValues(
                alpha: 0.9,
              ),
              foregroundColor: CupertinoColors.white,
              icon: CupertinoIcons.pencil,
              label: 'Edit',
              borderRadius: BorderRadius.circular(itemRadius),
            ),
            //deletebutton
            SlidableAction(
              onPressed: (_) => deleteDance(dance.id),
              backgroundColor: CupertinoColors.systemRed.withValues(alpha: 0.9),
              foregroundColor: CupertinoColors.white,
              icon: CupertinoIcons.trash,
              label: 'Delete',
              borderRadius: BorderRadius.circular(itemRadius),
            ),
          ],
        ),
        //listItemMain体 - optimizestyle
        child: Obx(
          () => Container(
            height: itemHeight,
            decoration: BoxDecoration(
              //渐变background + Shadow
              gradient: LinearGradient(
                begin: Alignment.topLeft,
                end: Alignment.bottomRight,
                colors: [
                  dance.id == model.runId.value
                      ? CupertinoColors.systemPink.withValues(alpha: 0.1)
                      : CupertinoColors.systemOrange.withValues(alpha: 0.1),
                  dance.id == model.runId.value
                      ? CupertinoColors.systemPink.withValues(alpha: 0.3)
                      : CupertinoColors.systemOrange.withValues(alpha: 0.3),
                ],
              ),
              borderRadius: BorderRadius.circular(itemRadius),
              boxShadow: [
                BoxShadow(
                  color: CupertinoColors.black.withValues(alpha: 0.06),
                  blurRadius: 10,
                  offset: const Offset(0, 3),
                ),
              ],
              border: Border.all(
                color: dance.id == model.runId.value
                    ? CupertinoColors.systemPink.withValues(alpha: 0.2)
                    : CupertinoColors.systemOrange.withValues(alpha: 0.2),
                width: 0.8,
              ),
            ),
            child: dance.isLoading
                ? Center(child: CupertinoActivityIndicator())
                : CupertinoButton(
                    padding: const EdgeInsets.symmetric(horizontal: 16),
                    borderRadius: BorderRadius.circular(itemRadius),
                    onPressed: () {
                      if (dance.id != null) {
                        if (dance.id == model.runId.value) {
                          stopPlay();
                        } else {
                          stopPlay();
                          model.runId.value = dance.id!;
                          startPlay();
                        }
                      }
                    },
                    child: Row(
                      children: [
                        Expanded(
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              //dancename
                              Text(
                                dance.danceName ?? "Untitled Dance",
                                style: TextStyle(
                                  color: textColor,
                                  fontSize: 18,
                                  fontWeight: FontWeight.w700,
                                  letterSpacing: 0.2,
                                ),
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                              ),
                              const SizedBox(height: 8),
                              //musicinfo
                              if (dance.musicInfo?.title != null)
                                Row(
                                  children: [
                                    SvgPicture.asset(
                                      "assets/music.note.svg",
                                      colorFilter: ColorFilter.mode(
                                        subTextColor,
                                        BlendMode.srcIn,
                                      ),
                                      width: 16,
                                      height: 16,
                                    ),
                                    const SizedBox(width: 6),
                                    Expanded(
                                      child: Text(
                                        dance.musicInfo!.title ?? "",
                                        style: TextStyle(
                                          color: subTextColor,
                                          fontSize: 14,
                                          height: 1.2,
                                        ),
                                        maxLines: 1,
                                        overflow: TextOverflow.ellipsis,
                                      ),
                                    ),
                                  ],
                                ),
                              //playstatehint
                              if (dance.id == model.runId.value)
                                Padding(
                                  padding: const EdgeInsets.only(top: 6),
                                  child: Text(
                                    "Now Playing",
                                    style: TextStyle(
                                      color: dance.id == model.runId.value
                                          ? CupertinoColors.systemPink
                                          : primaryColor,
                                      fontSize: 12,
                                      fontWeight: FontWeight.w600,
                                    ),
                                  ),
                                ),
                            ],
                          ),
                        ),
                        //musiccover
                        if (dance.musicInfo?.artwork != null)
                          ClipRRect(
                            borderRadius: BorderRadius.circular(12),
                            child: Image.file(
                              File(dance.musicInfo!.artwork!),
                              width: 70,
                              height: 70,
                              fit: BoxFit.cover,
                            ),
                          ),
                      ],
                    ),
                  ),
          ),
        ),
      );
    }
  }

  Future<void> addDance(
    List<DanceData> danceList,
    String url,
    String title,
  ) async {
    final Map<String, dynamic> map = {
      ValueConstant.danceData: DanceData.listToJson(danceList),
      ValueConstant.danceName: title,
      ValueConstant.musicUrl: url,
      ValueConstant.mac: AppState.shared.deviceMac,
    };
    final response = await Http.instance.post(Urls.v2dance, data: map);
    if (response.data != null) {
      Model<String> responseData = Model.fromJsonT(response.data);
      if (responseData.isSuccess()) {
        getDanceList();
      }
    }
  }

  void sendDanceData(List<DanceData> danceList) {
    final jsonString = jsonEncode(DanceData.listToJson(danceList));
    AppState.shared.sendWebSocketMessage(
      .dance,
      data: jsonString.toUint8List(),
    );
  }

  Future<void> startPlay() async {
    if (isPlaying) {
      stopPlay();
      return;
    }

    if (model.runId.value == -1) {
      MusicUtil.shared.stopMusic();
      isPlaying = false;
      return;
    }

    final currentDance = model.list.firstWhere(
      (dance) => dance.id == model.runId.value,
      orElse: () => DanceList(),
    );

    if (currentDance.id == null || currentDance.danceData.isEmpty) {
      AppState.shared.showToast(
        "The current dance data is empty and cannot be played.",
      );
      stopPlay();
      return;
    }

    final danceList = currentDance.danceData;
    isPlaying = true;
    MusicUtil.shared.stopMusic();

    //checkmusicinfoandfilewhetherhas效
    var musicInfo = currentDance.musicInfo;
    if (musicInfo == null || !(await File(musicInfo.filePath).exists())) {
      if (currentDance.musicUrl != null && currentDance.musicUrl!.isNotEmpty) {
        //musicinfoinvalidorfilenot exist，reGet
        currentDance.isLoading = true;
        model.list.refresh();
        musicInfo = await MusicUtil.shared.getMusicInfoAsync(
          currentDance.musicUrl!,
        );
        currentDance.musicInfo = musicInfo;
        currentDance.isLoading = false;
        model.list.refresh();
      }

      if (musicInfo == null) {
        AppState.shared.showToast(
          "Music file is missing, please try again later.",
        );
        stopPlay();
        return;
      }
    }
    MusicUtil.shared.playMusic(musicInfo);
    final currentPlayId = model.runId.value;
    final currentLoopMode = model.isLoopMode.value;
    if (AppState.shared.deviceControlMode == 0) {
      sendDanceData(danceList);

      int totalDurationMs = danceList.fold(
        0,
        (sum, data) => sum + (data.durationMs),
      );
      double totalDurationSeconds = totalDurationMs / 1000.0;

      _playTimer = Timer(Duration(seconds: totalDurationSeconds.round()), () {
        if (isPlaying && model.runId.value == currentPlayId) {
          if (currentLoopMode) {
            isPlaying = false;
            startPlay(); //async调用，无需等待
          } else {
            stopPlay();
          }
        }
      });
    } else if (AppState.shared.deviceControlMode == 1) {
      _playBluetoothDance(danceList, currentPlayId, currentLoopMode);
    }
  }

  Future<void> _playBluetoothDance(
    List<DanceData> danceList,
    int currentPlayId,
    bool currentLoopMode,
  ) async {
    final task = _playBluetoothFrames(
      danceList,
      currentPlayId,
      currentLoopMode,
    );
    _bluetoothPlayTasks.add(task);
    await task;
    _bluetoothPlayTasks.remove(task);
  }

  Future<void> _playBluetoothFrames(
    List<DanceData> danceList,
    int currentPlayId,
    bool currentLoopMode,
  ) async {
    for (var danceData in danceList) {
      if (!isPlaying || model.runId.value != currentPlayId) {
        break;
      }

      await BlueUtil.shared.sendDanceData(danceData);

      int waitMs = (danceData.durationMs) + 90;
      await Future.delayed(Duration(milliseconds: waitMs));
    }

    if (isPlaying && model.runId.value == currentPlayId) {
      if (currentLoopMode) {
        isPlaying = false;
        startPlay(); //async调用，无需等待
      } else {
        stopPlay();
      }
    }
  }

  void stopPlay() {
    isPlaying = false;
    model.runId.value = -1;
    MusicUtil.shared.stopMusic();

    _playTimer?.cancel();
    _playTimer = null;

    for (var task in _bluetoothPlayTasks) {
      task?.ignore();
    }
    _bluetoothPlayTasks.clear();
  }

  Future<void> deleteDance(int? id) async {
    if (id != null) {
      Map<String, dynamic> map = {ValueConstant.id: id};
      final response = await Http.instance.delete(Urls.v2dance, data: map);
      if (response.data != null) {
        Model<String> responseData = Model.fromJsonT(response.data);
        if (responseData.isSuccess()) {
          getDanceList();
        }
      }
    }
  }

  Future<void> getDanceList() async {
    final map = {ValueConstant.mac: AppState.shared.deviceMac};
    final response = await Http.instance.get(Urls.v2dance, data: map);
    Model<List<DanceList>> responseData = Model.fromJsonT(
      response.data,
      factory: (data) => DanceList.fromListJson(data),
    );
    if (responseData.isSuccess() && responseData.data != null) {
      final list = responseData.data!;
      model.list.value = list;
      getMusicInfo();
    }
  }

  Future<void> getMusicInfo() async {
    for (int i = 0; i < model.list.length; i++) {
      if (model.list[i].musicUrl != null &&
          model.list[i].musicUrl!.isNotEmpty) {
        model.list[i].isLoading = true;
        model.list.refresh();
        final musicInfo = await MusicUtil.shared.getMusicInfoAsync(
          model.list[i].musicUrl!,
        );
        if (musicInfo != null) {
          model.list[i].musicInfo = musicInfo;
        }
        model.list[i].isLoading = false;
        model.list.refresh();
      }
    }
    model.list.refresh();
  }
}

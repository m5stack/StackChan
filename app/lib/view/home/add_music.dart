/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:typed_data';

import 'package:dio/dio.dart';
import 'package:file_picker/file_picker.dart';
import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/theme_setting.dart';
import 'package:flutter_svg/svg.dart';
import 'package:path/path.dart' as path;
import 'package:uuid/uuid.dart';

import '../../app_state.dart';
import '../../model/model.dart';
import '../../model/stack_chan_music_info.dart';
import '../../model/upload_file_data.dart';
import '../../network/http.dart';
import '../../network/urls.dart';
import '../../util/value_constant.dart';

class AddMusic extends StatefulWidget {
  const AddMusic({super.key, required this.onResult});

  final FutureOr<void> Function(String url) onResult;

  @override
  State<StatefulWidget> createState() => _AddMusicState();
}

class _AddMusicState extends State<AddMusic> {
  String musicURL = "";

  final Dio _dio = Dio();

  final List<StackChanMusicInfo> _musicList = [
    StackChanMusicInfo(
      name: "StackChan on My Desk",
      url: "${Urls.getFileUrl()}file/music/stackchan_music.mp3",
    ),
  ];

  @override
  void dispose() {
    super.dispose();
  }

  Future<void> _completeSelection(String url) async {
    await widget.onResult(url);
    if (mounted) {
      Navigator.maybePop(context);
    }
  }

  void _checkLink() {
    final url = musicURL;
    if (url.isEmpty) {
      AppState.shared.showToast("Please enter the music link.");
      return;
    }

    if (!Uri.parse(url).isAbsolute) {
      AppState.shared.showToast("Please provide a valid URL link.");
      return;
    }
    _downloadAndUploadFile(Uri.parse(url));
  }

  Future<void> _downloadAndUploadFile(Uri url) async {
    try {
      final response = await _dio.getUri(
        url,
        options: Options(
          responseType: ResponseType.bytes,
          followRedirects: true,
          maxRedirects: 5,
          headers: {"Accept": "audio/mpeg,audio/*,*/*;q=0.9"},
        ),
      );
      if (response.statusCode != 200) {
        AppState.shared.showToast(
          "Download failed, status code: ${response.statusCode}",
        );
        return;
      }

      if (response.data == null || response.data is! Uint8List) {
        AppState.shared.showToast("Download failed: Invalid response data");
        return;
      }

      final fileName = _generateUUIDFileName(url.path);
      await _uploadFile(response.data as Uint8List, fileName);
    } catch (e) {
      AppState.shared.showToast("Download failed: ${e.toString()}");
    }
  }

  Future<void> _pickLocalFile() async {
    try {
      final file = await FilePicker.pickFile(
        type: FileType.custom,
        allowedExtensions: ['mp3', 'wav', 'm4a'],
        dialogTitle: "Select audio file",
      );

      if (file == null) return;

      final Uint8List fileData = await file.readAsBytes();
      final fileName = _generateUUIDFileName(file.name);
      await _uploadFile(fileData, fileName);
    } catch (e) {
      AppState.shared.showToast("File selection failed: ${e.toString()}");
    }
  }

  Future<void> _uploadFile(Uint8List data, String fileName) async {
    try {
      FormData formData = FormData.fromMap({
        ValueConstant.file: MultipartFile.fromBytes(
          data,
          filename: fileName,
          contentType: DioMediaType.parse("audio/mpeg"),
        ),
        ValueConstant.directory: ValueConstant.moments,
        ValueConstant.name: fileName,
      });

      final response = await Http.instance.postFormData(
        Urls.uploadFile,
        formData,
      );

      if (response.data != null) {
        Model<UploadFile> responseData = Model.fromJsonT(
          response.data,
          factory: (data) => UploadFile.fromJson(data),
        );
        if (responseData.isSuccess()) {
          String? url = responseData.data?.path;
          if (url != null) {
            final fileUrl = Urls.getFileUrl() + url;
            await _completeSelection(fileUrl);
          } else {
            AppState.shared.showToast("Upload failed: File path is empty");
          }
        } else {
          AppState.shared.showToast(responseData.message ?? "Upload failed");
        }
      } else {
        AppState.shared.showToast("Upload failed: Empty response");
      }
    } catch (e) {
      AppState.shared.showToast("Upload failed: ${e.toString()}");
    }
  }

  String _generateUUIDFileName(String originalPath) {
    final fileExtension = path.extension(originalPath).isEmpty
        ? 'mp3'
        : path.extension(originalPath).replaceFirst('.', '');
    return "${const Uuid().v4()}.$fileExtension";
  }

  @override
  Widget build(BuildContext context) {
    final theme = CupertinoTheme.of(context);
    final padding = MediaQuery.paddingOf(context);

    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      child: CustomScrollView(
        slivers: [
          CupertinoSliverNavigationBar(
            stretch: true,
            backgroundColor: ThemeSetting.cupertinoNavigationBarBackgroundColor(
              context,
            ),
            border: ThemeSetting.cupertinoNavigationBarBorder(context),
            padding: ThemeSetting.navigationBarPadding,
            largeTitle: const Text("Add Music"),
            leading: const SizedBox.shrink(),
            trailing: CupertinoButton(
              sizeStyle: .medium,
              onPressed: () {
                Navigator.maybePop(context);
              },
              child: const Icon(CupertinoIcons.xmark),
            ),
          ),
          SliverPadding(
            padding: .only(left: padding.left, right: padding.right),
            sliver: SliverList.list(
              children: [
                CupertinoListSection.insetGrouped(
                  header: const Text("URL"),
                  children: [
                    CupertinoListTile(
                      leading: SvgPicture.asset(
                        "assets/music.note.svg",
                        colorFilter: .mode(theme.primaryColor, .srcIn),
                        width: 15,
                        height: 15,
                      ),
                      title: Row(
                        children: [
                          const Spacer(),
                          SizedBox(
                            width: 220,
                            child: CupertinoTextField(
                              onChanged: (value) {
                                musicURL = value;
                              },
                              placeholder: "Enter the music link",
                              textAlign: .end,
                              decoration: const BoxDecoration(),
                            ),
                          ),
                        ],
                      ),
                      trailing: CupertinoButton(
                        sizeStyle: .small,
                        padding: .zero,
                        minimumSize: .zero,
                        child: SvgPicture.asset(
                          "assets/checkmark.svg",
                          colorFilter: .mode(theme.primaryColor, .srcIn),
                          width: 15,
                          height: 15,
                        ),
                        onPressed: () {
                          _checkLink();
                        },
                      ),
                    ),
                  ],
                ),
                CupertinoListSection.insetGrouped(
                  header: const Text("File"),
                  children: [
                    CupertinoListTile(
                      onTap: () => _pickLocalFile(),
                      title: const Text("Select local music files"),
                      leading: SvgPicture.asset(
                        "assets/music.note.svg",
                        colorFilter: .mode(theme.primaryColor, .srcIn),
                        width: 15,
                        height: 15,
                      ),
                      trailing: SvgPicture.asset(
                        "assets/chevron.right.svg",
                        colorFilter: const .mode(
                          CupertinoColors.secondaryLabel,
                          .srcIn,
                        ),
                        width: 15,
                        height: 15,
                      ),
                    ),
                  ],
                ),
                CupertinoListSection.insetGrouped(
                  header: const Text("Prefabricated"),
                  children: _musicList.indexed.map((entry) {
                    final value = entry.$2;
                    return CupertinoListTile(
                      leading: SvgPicture.asset(
                        "assets/music.note.svg",
                        colorFilter: .mode(theme.primaryColor, .srcIn),
                        width: 15,
                        height: 15,
                      ),
                      title: Text(value.name),
                      onTap: () {
                        _completeSelection(value.url);
                      },
                    );
                  }).toList(),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

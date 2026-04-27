/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:io';
import 'dart:typed_data';

import 'package:dio/dio.dart';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
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

  final Function(String url) onResult;

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
    if (mounted) {
      FocusScope.of(context).unfocus();
    }
    super.dispose();
  }

  void _completeSelection(String url) {
    widget.onResult(url);
    Navigator.pop(context);
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
      final result = await FilePicker.pickFiles(
        type: FileType.custom,
        allowedExtensions: ['mp3', 'wav', 'm4a'],
        dialogTitle: "Select audio file",
      );

      if (result == null || result.files.isEmpty) return;

      final file = result.files.first;
      Uint8List? fileData;

      if (file.path != null) {
        fileData = File(file.path!).readAsBytesSync();
      } else {
        fileData = file.bytes;
      }

      if (fileData == null) {
        AppState.shared.showToast("No file data was found");
        return;
      }
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
            _completeSelection(fileUrl);
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

    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      navigationBar: CupertinoNavigationBar.large(
        largeTitle: Text("Add Music"),
        trailing: CupertinoButton(
          sizeStyle: .medium,
          child: Icon(CupertinoIcons.xmark),
          onPressed: () => Navigator.of(context).pop(),
        ),
      ),
      child: ListView(
        children: [
          CupertinoListSection.insetGrouped(
            header: Text("URL"),
            children: [
              CupertinoListTile(
                leading: SvgPicture.asset(
                  "assets/music.note.svg",
                  colorFilter: .mode(theme.primaryColor, .srcIn),
                  width: 15,
                  height: 15,
                ),
                trailing: Row(
                  mainAxisSize: .min,
                  children: [
                    SizedBox(
                      width: 250,
                      child: CupertinoTextField(
                        onChanged: (value) {
                          musicURL = value;
                        },
                        placeholder: "Enter the music link",
                        textAlign: .end,
                        decoration: BoxDecoration(),
                      ),
                    ),
                    CupertinoButton(
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
                  ],
                ),
                title: SizedBox.shrink(),
              ),
            ],
          ),
          CupertinoListSection.insetGrouped(
            header: Text("File"),
            children: [
              CupertinoListTile(
                onTap: () => _pickLocalFile(),
                title: Text("Select local music files"),
                leading: SvgPicture.asset(
                  "assets/music.note.svg",
                  colorFilter: .mode(theme.primaryColor, .srcIn),
                  width: 15,
                  height: 15,
                ),
                trailing: SvgPicture.asset(
                  "assets/chevron.right.svg",
                  colorFilter: .mode(CupertinoColors.secondaryLabel, .srcIn),
                  width: 15,
                  height: 15,
                ),
              ),
            ],
          ),
          CupertinoListSection.insetGrouped(
            header: Text("Prefabricated"),
            children: _musicList.map((value) {
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
    );
  }
}

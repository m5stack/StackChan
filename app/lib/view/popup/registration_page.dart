/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:get/get.dart';

import '../../app_state.dart';
import '../../model/model.dart';
import '../../model/registration_response.dart';
import '../../network/http.dart';
import '../../network/urls.dart';
import '../../util/value_constant.dart';
import '../util/theme_setting.dart';

/// Registration page.
class RegistrationPage extends StatefulWidget {
  const RegistrationPage({super.key});

  @override
  State<StatefulWidget> createState() => _RegistrationPageState();
}

class _RegistrationPageState extends State<RegistrationPage> {
  late final TextEditingController usernameController;
  late final TextEditingController passwordController;
  late final TextEditingController emailController;

  final RxnString usernameErrorMessage = RxnString();

  @override
  void initState() {
    super.initState();
    usernameController = TextEditingController();
    passwordController = TextEditingController();
    emailController = TextEditingController();
  }

  @override
  void dispose() {
    usernameController.dispose();
    passwordController.dispose();
    emailController.dispose();
    super.dispose();
  }

  Future<void> register() async {
    final username = usernameController.text.trim();
    final password = passwordController.text.trim();
    final email = emailController.text.trim();

    usernameErrorMessage.value = null;

    if (username.isEmpty) {
      AppState.shared.showToast("Please enter your username.");
      return;
    }
    if (password.isEmpty) {
      AppState.shared.showToast("Please enter the password.");
      return;
    }
    if (email.isEmpty || !email.contains('@')) {
      AppState.shared.showToast("Please enter a valid email address.");
      return;
    }

    final Map<String, dynamic> requestData = {
      ValueConstant.username: username,
      ValueConstant.password: password,
      ValueConstant.email: email,
    };

    final response = await Http.instance.post(
      Urls.registration,
      data: requestData,
    );

    if (response.data != null) {
      Model<RegistrationResponse> responseData = Model.fromJsonT(
        response.data,
        factory: (value) => RegistrationResponse.fromJson(value),
      );
      if (responseData.isSuccess()) {
        final registeredUser = responseData.data;
        AppState.shared.showToast("Registration successful!");
        if (mounted) {
          showCupertinoDialog(
            barrierDismissible: false,
            context: context,
            builder: (context) {
              return CupertinoAlertDialog(
                title: const Text("Registration successful!"),
                content: Text(
                  _registrationSuccessMessage(registeredUser),
                  textAlign: TextAlign.left,
                ),
                actions: [
                  CupertinoDialogAction(
                    child: const Text("Got it."),
                    onPressed: () {
                      Navigator.maybePop(context);
                      Navigator.maybePop(this.context);
                    },
                  ),
                ],
              );
            },
          );
        }
      } else if (responseData.code == 300 &&
          responseData.message?.contains(
                "Username is already registered or unavailable",
              ) ==
              true) {
        usernameErrorMessage.value = "Username is already taken.";
      } else {
        showLoginErrMessage(responseData.message);
      }
    } else {
      AppState.shared.showToast(response.statusMessage ?? "Register failed");
    }
  }

  String _registrationSuccessMessage(RegistrationResponse? user) {
    final lines = <String>[
      "Before logging in, you must confirm your email address.",
    ];

    if (user == null) {
      return lines.join("\n");
    }

    final username = _nonEmptyText(user.username);
    final displayName = _nonEmptyText(user.displayname);
    final userSlug = _nonEmptyText(user.userslug);
    final uid = _nonEmptyText(user.uid);
    final status = _nonEmptyText(user.userStatus);

    lines.add("");
    lines.add("Registered user:");
    if (username != null) {
      lines.add("Username: $username");
    }
    if (displayName != null) {
      lines.add("Display name: $displayName");
    }
    if (userSlug != null) {
      lines.add("User slug: $userSlug");
    }
    if (uid != null) {
      lines.add("UID: $uid");
    }
    if (status != null) {
      lines.add("Status: $status");
    }

    return lines.join("\n");
  }

  String? _nonEmptyText(Object? value) {
    final text = value?.toString().trim();
    if (text == null || text.isEmpty) {
      return null;
    }
    return text;
  }

  void showLoginErrMessage(String? text) {
    // Fallback message when the server sends nothing usable.
    String errorMessage = "Register failed";

    if (text != null && text.isNotEmpty) {
      // Match the [[error:xxx]] wrapper and extract the message inside it.
      final regExp = RegExp(r'\[\[error:(.*?)\]\]');
      final match = regExp.firstMatch(text);

      if (match != null) {
        // Use the server-provided error text.
        errorMessage = match.group(1)!.trim();
      } else {
        // Plain text error, use it as-is.
        errorMessage = text.trim();
      }
    }
    // Show a toast.
    AppState.shared.showToast(errorMessage);
  }

  @override
  Widget build(BuildContext context) {
    final padding = MediaQuery.paddingOf(context);

    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      child: Column(
        children: [
          Expanded(
            child: CustomScrollView(
              slivers: [
                CupertinoSliverNavigationBar(
                  backgroundColor:
                      ThemeSetting.cupertinoNavigationBarBackgroundColor(
                        context,
                      ),
                  border: ThemeSetting.cupertinoNavigationBarBorder(context),
                  padding: ThemeSetting.navigationBarPadding,
                  largeTitle: const Text("Register"),
                  stretch: true,
                  leading: CupertinoButton(
                    sizeStyle: .medium,
                    child: const Icon(CupertinoIcons.back),
                    onPressed: () {
                      Navigator.maybePop(context);
                    },
                  ),
                ),
                SliverPadding(
                  padding: .only(left: padding.left, right: padding.right),
                  sliver: SliverList.list(
                    children: [
                      CupertinoListSection.insetGrouped(
                        header: Obx(() {
                          final errorMessage = usernameErrorMessage.value;
                          return Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              const Text("Username"),
                              if (errorMessage != null)
                                Padding(
                                  padding: const EdgeInsets.only(top: 4),
                                  child: Text(
                                    errorMessage,
                                    style: TextStyle(
                                      color: CupertinoColors.destructiveRed
                                          .resolveFrom(context),
                                      fontSize: 12,
                                      fontWeight: FontWeight.w500,
                                    ),
                                  ),
                                ),
                            ],
                          );
                        }),
                        children: [
                          CupertinoTextField(
                            controller: usernameController,
                            placeholder: "username",
                            placeholderStyle: TextStyle(
                              fontSize: 20,
                              color: CupertinoColors.placeholderText
                                  .resolveFrom(context),
                            ),
                            style: TextStyle(
                              fontSize: 20,
                              color: CupertinoColors.label.resolveFrom(context),
                            ),
                            keyboardType: .name,
                            padding: const .only(
                              left: 20,
                              right: 20,
                              top: 10,
                              bottom: 10,
                            ),
                            decoration: const BoxDecoration(),
                            autofocus: true,
                            textInputAction: .next,
                            onChanged: (_) {
                              usernameErrorMessage.value = null;
                            },
                          ),
                        ],
                      ),
                      CupertinoListSection.insetGrouped(
                        header: const Text("Password"),
                        children: [
                          CupertinoTextField(
                            controller: passwordController,
                            placeholder: "password",
                            placeholderStyle: TextStyle(
                              fontSize: 20,
                              color: CupertinoColors.placeholderText
                                  .resolveFrom(context),
                            ),
                            style: TextStyle(
                              fontSize: 20,
                              color: CupertinoColors.label.resolveFrom(context),
                            ),
                            keyboardType: .visiblePassword,
                            padding: const .only(
                              left: 20,
                              right: 20,
                              top: 10,
                              bottom: 10,
                            ),
                            decoration: const BoxDecoration(),
                            autofocus: true,
                            textInputAction: .next,
                          ),
                        ],
                      ),
                      CupertinoListSection.insetGrouped(
                        header: const Text("Email"),
                        children: [
                          CupertinoTextField(
                            controller: emailController,
                            placeholder: "email",
                            placeholderStyle: TextStyle(
                              fontSize: 20,
                              color: CupertinoColors.placeholderText
                                  .resolveFrom(context),
                            ),
                            style: TextStyle(
                              fontSize: 20,
                              color: CupertinoColors.label.resolveFrom(context),
                            ),
                            keyboardType: .emailAddress,
                            padding: const .only(
                              left: 20,
                              right: 20,
                              top: 10,
                              bottom: 10,
                            ),
                            decoration: const BoxDecoration(),
                            autofocus: true,
                            textInputAction: .go,
                            onSubmitted: (value) {
                              if (mounted) {
                                FocusScope.of(context).unfocus();
                              }
                              register();
                            },
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
          Padding(
            padding: .only(
              top: 20,
              left: 20 + padding.left,
              right: 20 + padding.right,
              bottom: 20 + padding.bottom,
            ),
            child: SizedBox(
              width: .infinity,
              child: CupertinoButton.filled(
                child: const Text(
                  "Submit",
                  style: TextStyle(color: CupertinoColors.white),
                ),
                onPressed: () {
                  if (mounted) {
                    FocusScope.of(context).unfocus();
                  }
                  register();
                },
              ),
            ),
          ),
        ],
      ),
    );
  }
}

/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/model/model.dart';
import 'package:stack_chan/network/http.dart';
import 'package:stack_chan/network/urls.dart';
import 'package:stack_chan/util/value_constant.dart';
import 'package:stack_chan/view/app.dart';
import 'package:stack_chan/view/home/web_page.dart';
import 'package:stack_chan/view/popup/registration_page.dart';
import 'package:stack_chan/view/util/popup.dart';
import 'package:stack_chan/view/util/theme_setting.dart';

class LoginPage extends StatefulWidget {
  const LoginPage({super.key, this.isWelCome});

  final bool? isWelCome;

  @override
  State<StatefulWidget> createState() => _LoginPageState();
}

/// Response payload returned by the login endpoint.
class LoginResponseModel {
  String? token;

  /// Creates a login response with an optional token.
  LoginResponseModel({this.token});

  /// Builds a login response from a decoded JSON map.
  factory LoginResponseModel.fromJson(Map<String, dynamic> json) {
    return LoginResponseModel(token: json['token'] as String?);
  }

  /// Encodes this response as a JSON map.
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['token'] = token;
    return data;
  }
}

class _LoginPageState extends State<LoginPage> {
  String username = "";
  String password = "";

  late TextEditingController nameTextEditingController;
  late TextEditingController passwordTextEditingController;

  bool _obscurePassword = true;

  RxBool loading = false.obs;

  late FocusNode passwordFocusNode;

  @override
  void initState() {
    super.initState();
    nameTextEditingController = TextEditingController();
    passwordTextEditingController = TextEditingController();
    passwordFocusNode = FocusNode();
  }

  @override
  void dispose() {
    FocusManager.instance.primaryFocus?.unfocus();
    nameTextEditingController.dispose();
    passwordTextEditingController.dispose();
    passwordFocusNode.dispose();
    super.dispose();
  }

  Future<void> login() async {
    TextInput.finishAutofillContext();
    if (loading.value) {
      return;
    }
    loading.value = true;
    if (username.isEmpty) {
      AppState.shared.showToast(
        "Please enter your account number or email address.",
      );
      loading.value = false;
      return;
    }
    if (password.isEmpty) {
      AppState.shared.showToast("Please enter the password.");
      loading.value = false;
      return;
    }
    final Map<String, dynamic> map = {
      ValueConstant.username: username,
      ValueConstant.password: password,
    };
    final response = await Http.instance.post(Urls.login, data: map);
    if (response.data != null) {
      Model<LoginResponseModel> responseData = Model.fromJsonT(
        response.data,
        factory: (value) => LoginResponseModel.fromJson(value),
      );
      if (responseData.isSuccess()) {
        final token = responseData.data?.token;
        if (token != null) {
          AppState.shared.setIsLogin(true);
          await AppState.asyncPrefs.setString(ValueConstant.token, token);
          AppState.shared.showToast("Login successful");
          if (mounted) {
            AppState.shared.getUserInfo();
            AppState.shared.getDevices();
            if (widget.isWelCome != true) {
              closeCustomCupertinoSheet(context);
            } else {
              Navigator.maybePop(context);
            }
          }
        }
      } else {
        showLoginErrMessage(responseData.message);
      }
    } else {
      AppState.shared.showToast(response.statusMessage);
    }
    loading.value = false;
  }

  void showLoginErrMessage(String? text) {
    if (text == "[[error:invalid-login-credentials]]") {
      /// Invalid credentials; the dialog also warns about the one-hour lockout.
      App.showDialog(
        "Incorrect password or account number (the account will be locked for one hour after five incorrect attempts)",
      );
      return;
    } else if (text == "[[error:account-locked]]") {
      App.showDialog("Account will be frozen for 1 hour after 5 failures");
      return;
    } else {
      // Fallback message when the server sends nothing usable.
      String errorMessage = "Login failed";

      if (text != null && text.isNotEmpty) {
        // Match the [[error:xxx]] wrapper and extract the message inside.
        final regExp = RegExp(r'\[\[error:(.*?)\]\]');
        final match = regExp.firstMatch(text);

        if (match != null) {
          // Use the server-provided error text.
          errorMessage = match.group(1)!.trim();
        } else {
          // Unwrapped error text: use it as-is.
          errorMessage = text.trim();
        }
      }
      // Show a toast.
      AppState.shared.showToast(errorMessage);
    }
  }

  @override
  Widget build(BuildContext context) {
    final mediaQueryPadding = MediaQuery.paddingOf(context);

    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      child: Obx(() {
        if (loading.value) {
          return const Center(child: CupertinoActivityIndicator());
        } else {
          return Column(
            children: [
              Expanded(
                child: CustomScrollView(
                  slivers: [
                    CupertinoSliverNavigationBar(
                      backgroundColor:
                          ThemeSetting.cupertinoNavigationBarBackgroundColor(
                            context,
                          ),
                      border: ThemeSetting.cupertinoNavigationBarBorder(
                        context,
                      ),
                      leading: const SizedBox.shrink(),
                      padding: ThemeSetting.navigationBarPadding,
                      stretch: true,
                      largeTitle: const Text("Login"),
                      trailing: widget.isWelCome != true
                          ? CupertinoButton(
                              sizeStyle: .medium,
                              child: const Icon(CupertinoIcons.xmark),
                              onPressed: () {
                                Navigator.of(
                                  context,
                                  rootNavigator: true,
                                ).maybePop();
                              },
                            )
                          : const SizedBox.shrink(),
                    ),
                    SliverPadding(
                      padding: .only(
                        left: mediaQueryPadding.left,
                        right: mediaQueryPadding.right,
                      ),
                      sliver: SliverList.list(
                        children: [
                          Row(
                            children: [
                              const SizedBox(width: 20),
                              Image.asset(
                                "assets/lateral_image.png",
                                height: 120,
                                width: 120,
                              ),
                              const SizedBox(width: 20),
                              const Expanded(
                                child: Text(
                                  "M5Stack account is used across UiFlow, M5Burner and forum 😊",
                                ),
                              ),
                              const SizedBox(width: 20),
                            ],
                          ),
                          const SizedBox(height: 10),
                          AutofillGroup(
                            child: Column(
                              mainAxisSize: .min,
                              spacing: 0,
                              children: [
                                CupertinoListSection.insetGrouped(
                                  header: const Text("Username or Email"),
                                  children: [
                                    CupertinoTextField(
                                      placeholder: "Username or Email",
                                      controller: nameTextEditingController,
                                      placeholderStyle: const TextStyle(
                                        fontSize: 20,
                                        color: CupertinoColors.placeholderText,
                                      ),
                                      style: const TextStyle(
                                        fontSize: 20,
                                        color: CupertinoColors.label,
                                      ),
                                      keyboardType: .emailAddress,
                                      padding: const .only(
                                        left: 20,
                                        right: 20,
                                        top: 10,
                                        bottom: 10,
                                      ),
                                      decoration: const BoxDecoration(),
                                      textInputAction: .next,
                                      onChanged: (value) {
                                        username = value;
                                      },
                                      autofillHints: const [
                                        AutofillHints.username,
                                      ],
                                      onSubmitted: (_) {
                                        passwordFocusNode.requestFocus();
                                      },
                                    ),
                                  ],
                                ),
                                CupertinoListSection.insetGrouped(
                                  header: Row(
                                    children: [
                                      const Text("Password"),
                                      const Spacer(),
                                      CupertinoButton(
                                        padding: .zero,
                                        child: const Text(
                                          "Forget password",
                                          style: TextStyle(fontSize: 12),
                                        ),
                                        onPressed: () {
                                          Navigator.push(
                                            context,
                                            CupertinoPageRoute(
                                              builder: (context) {
                                                return const WebPage(
                                                  url: "https://community.m5stack.com/reset",
                                                );
                                              },
                                            ),
                                          );
                                        },
                                      ),
                                    ],
                                  ),
                                  children: [
                                    CupertinoTextField(
                                      focusNode: passwordFocusNode,
                                      controller: passwordTextEditingController,
                                      placeholder: "Password",
                                      obscureText: _obscurePassword,
                                      style: const TextStyle(
                                        fontSize: 20,
                                        color: CupertinoColors.label,
                                      ),
                                      placeholderStyle: const TextStyle(
                                        fontSize: 20,
                                        color: CupertinoColors.placeholderText,
                                      ),
                                      padding: const .only(
                                        left: 20,
                                        right: 20,
                                        top: 10,
                                        bottom: 10,
                                      ),
                                      decoration: const BoxDecoration(),
                                      keyboardType: .visiblePassword,
                                      textInputAction: .go,
                                      onSubmitted: (value) {
                                        login();
                                      },
                                      onChanged: (value) {
                                        password = value;
                                      },
                                      autofillHints: const [
                                        AutofillHints.password,
                                      ],
                                      suffix: CupertinoButton(
                                        padding: const .only(
                                          left: 20,
                                          right: 20,
                                          top: 10,
                                          bottom: 10,
                                        ),
                                        child: Icon(
                                          _obscurePassword
                                              ? CupertinoIcons.eye_slash
                                              : CupertinoIcons.eye,
                                          size: 22,
                                          color: CupertinoColors.systemGrey
                                              .resolveFrom(context),
                                        ),
                                        onPressed: () {
                                          setState(() {
                                            _obscurePassword =
                                                !_obscurePassword;
                                          });
                                        },
                                      ),
                                    ),
                                  ],
                                ),
                              ],
                            ),
                          ),
                          const SizedBox(height: 10),
                          Row(
                            spacing: 0,
                            children: [
                              const Spacer(),
                              const Text("Don't have an account? "),
                              CupertinoButton(
                                onPressed: () {
                                  TextInput.finishAutofillContext();
                                  if (mounted) {
                                    FocusScope.of(context).unfocus();
                                  }
                                  Navigator.push(
                                    context,
                                    CupertinoPageRoute(
                                      builder: (context) {
                                        return const RegistrationPage();
                                      },
                                    ),
                                  );
                                },
                                padding: .zero,
                                child: const Text("Register now"),
                              ),
                              const Spacer(),
                            ],
                          ),
                          const SizedBox(height: 10),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
              Padding(
                padding: .only(
                  top: 20,
                  left: 20 + mediaQueryPadding.left,
                  right: 20 + mediaQueryPadding.right,
                  bottom: 20 + mediaQueryPadding.bottom,
                ),
                child: SizedBox(
                  width: .infinity,
                  child: CupertinoButton.filled(
                    sizeStyle: .large,
                    child: const Text(
                      "Login",
                      style: TextStyle(color: CupertinoColors.white),
                    ),
                    onPressed: () {
                      login();
                    },
                  ),
                ),
              ),
            ],
          );
        }
      }),
    );
  }
}

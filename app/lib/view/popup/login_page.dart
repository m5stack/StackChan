/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/model/model.dart';
import 'package:stack_chan/model/registration_response.dart';
import 'package:stack_chan/network/http.dart';
import 'package:stack_chan/network/urls.dart';
import 'package:stack_chan/util/blue_util.dart';
import 'package:stack_chan/util/value_constant.dart';
import 'package:stack_chan/view/app.dart';

class LoginPage extends StatefulWidget {
  const LoginPage({super.key, this.isWelCome});

  final bool? isWelCome;

  @override
  State<StatefulWidget> createState() => _LoginPageState();
}

///loginAPIresponsemodel
class LoginResponseModel {
  String? token;

  ///Constructorfunction
  LoginResponseModel({this.token});

  ///from JSON maptoasmodelobject
  factory LoginResponseModel.fromJson(Map<String, dynamic> json) {
    return LoginResponseModel(token: json['token'] as String?);
  }

  ///willmodelobjecttoas JSON map
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

  @override
  void initState() {
    super.initState();
    nameTextEditingController = TextEditingController();
    passwordTextEditingController = TextEditingController();
  }

  @override
  void dispose() {
    nameTextEditingController.dispose();
    passwordTextEditingController.dispose();
    if (mounted) {
      FocusScope.of(context).unfocus();
    }
    super.dispose();
  }

  Future<void> login() async {
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
              CupertinoSheetRoute.popSheet(context);
            } else {
              Navigator.of(context).pop();
            }
            BlueUtil.shared.cachedDeviceMacs = [];
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
      ///passworderror
      App.showDialog(
        "Incorrect password or account number (the account will be locked for one hour after five incorrect attempts)",
      );
      return;
    } else if (text == "[[error:account-locked]]") {
      App.showDialog(
        "Your account has been locked. Please wait for a moment before trying again",
      );
      return;
    } else {
      //defaulthint
      String errorMessage = "Login failed";

      if (text != null && text.isNotEmpty) {
        //match [[error:xxx]] ,Extractinerrorcontent
        final regExp = RegExp(r'\[\[error:(.*?)\]\]');
        final match = regExp.firstMatch(text);

        if (match != null) {
          //Extracttocustomerrorcontent
          errorMessage = match.group(1)!.trim();
        } else {
          //texterror,directuse
          errorMessage = text.trim();
        }
      }
      // Toast
      AppState.shared.showToast(errorMessage);
    }
  }

  @override
  Widget build(BuildContext context) {
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      child: GestureDetector(
        onTap: () {
          if (mounted) {
            FocusScope.of(context).unfocus();
          }
        },
        behavior: .opaque,
        child: Obx(() {
          if (loading.value) {
            return Center(child: CupertinoActivityIndicator());
          } else {
            return Column(
              children: [
                Expanded(
                  child: CustomScrollView(
                    slivers: [
                      CupertinoSliverNavigationBar(
                        largeTitle: Text("Login"),
                        trailing: widget.isWelCome != true
                            ? CupertinoButton(
                                padding: .zero,
                                child: Icon(
                                  CupertinoIcons.xmark_circle_fill,
                                  size: 25,
                                  color: CupertinoColors.separator.resolveFrom(
                                    context,
                                  ),
                                ),
                                onPressed: () {
                                  Navigator.of(
                                    context,
                                    rootNavigator: true,
                                  ).pop();
                                },
                              )
                            : SizedBox.shrink(),
                      ),
                      SliverList.list(
                        children: [
                          Row(
                            children: [
                              SizedBox(width: 20),
                              Image.asset(
                                "assets/lateral_image.png",
                                height: 120,
                                width: 120,
                              ),
                              SizedBox(width: 20),
                              Expanded(
                                child: Text(
                                  "M5Stack account is used across UiFlow, M5Burner and forum 😊",
                                ),
                              ),
                              SizedBox(width: 20),
                            ],
                          ),
                          SizedBox(height: 15),
                          AutofillGroup(
                            child: Column(
                              mainAxisSize: .min,
                              spacing: 0,
                              children: [
                                CupertinoListSection.insetGrouped(
                                  header: Text("username or email"),
                                  children: [
                                    CupertinoListTile(
                                      padding: .zero,
                                      title: CupertinoTextField(
                                        placeholder: "username or email",
                                        controller: nameTextEditingController,
                                        placeholderStyle: TextStyle(
                                          fontSize: 20,
                                          color:
                                              CupertinoColors.placeholderText,
                                        ),
                                        style: TextStyle(
                                          fontSize: 20,
                                          color: CupertinoColors.label,
                                        ),
                                        keyboardType: .emailAddress,
                                        padding: .only(
                                          left: 20,
                                          right: 20,
                                          top: 15,
                                          bottom: 15,
                                        ),
                                        decoration: BoxDecoration(),
                                        autofocus: true,
                                        textInputAction: .next,
                                        onChanged: (value) {
                                          username = value;
                                        },
                                        autofillHints: const [
                                          AutofillHints.username,
                                        ],
                                      ),
                                    ),
                                  ],
                                ),
                                CupertinoListSection.insetGrouped(
                                  header: Text("password"),
                                  children: [
                                    CupertinoListTile(
                                      padding: .zero,
                                      title: CupertinoTextField(
                                        controller:
                                            passwordTextEditingController,
                                        placeholder: "password",
                                        obscureText: _obscurePassword,
                                        style: TextStyle(
                                          fontSize: 20,
                                          color: CupertinoColors.label,
                                        ),
                                        placeholderStyle: TextStyle(
                                          fontSize: 20,
                                          color:
                                              CupertinoColors.placeholderText,
                                        ),
                                        padding: .only(
                                          left: 20,
                                          right: 20,
                                          top: 15,
                                          bottom: 15,
                                        ),
                                        decoration: BoxDecoration(),
                                        keyboardType: .visiblePassword,
                                        textInputAction: .go,
                                        onSubmitted: (value) {
                                          TextInput.finishAutofillContext();
                                          login();
                                        },
                                        onChanged: (value) {
                                          password = value;
                                        },
                                        autofillHints: const [
                                          AutofillHints.password,
                                        ],
                                        suffix: CupertinoButton(
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
                                    ),
                                  ],
                                ),
                              ],
                            ),
                          ),
                          SizedBox(height: 15),
                          Row(
                            spacing: 0,
                            children: [
                              Spacer(),
                              Text("Don't have an account? "),
                              CupertinoButton(
                                onPressed: () {
                                  TextInput.finishAutofillContext();
                                  if (mounted) {
                                    FocusScope.of(context).unfocus();
                                  }
                                  Navigator.of(context).push(
                                    CupertinoPageRoute(
                                      builder: (context) {
                                        return RegistrationPage();
                                        // return WebPage(
                                        //   url:
                                        //       "https://community.m5stack.com/register",
                                        //   previousPageTitle: "Login",
                                        // );
                                      },
                                    ),
                                  );
                                },
                                minimumSize: .zero,
                                padding: .zero,
                                child: Text("Register now"),
                              ),
                              Spacer(),
                            ],
                          ),
                          SizedBox(height: 15),
                        ],
                      ),
                    ],
                  ),
                ),
                Padding(
                  padding: .only(
                    top: 15,
                    left: 15,
                    right: 15,
                    bottom: 15 + MediaQuery.paddingOf(context).bottom,
                  ),
                  child: CupertinoButton.filled(
                    sizeStyle: .large,
                    child: SizedBox(
                      width: .infinity,
                      child: Center(child: Text("Login")),
                    ),
                    onPressed: () {
                      login();
                    },
                  ),
                ),
              ],
            );
          }
        }),
      ),
    );
  }
}

class RegistrationPage extends StatefulWidget {
  const RegistrationPage({super.key});

  @override
  State<StatefulWidget> createState() => _RegistrationPageState();
}

class _RegistrationPageState extends State<RegistrationPage> {
  late final TextEditingController usernameController;
  late final TextEditingController passwordController;
  late final TextEditingController emailController;

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
        AppState.shared.showToast("Registration successful!");
        if (mounted) {
          Navigator.pop(context); //returnlogin
        }
      } else {
        showLoginErrMessage(responseData.message);
      }
    } else {
      AppState.shared.showToast(response.statusMessage ?? "Register failed");
    }
  }

  void showLoginErrMessage(String? text) {
    //defaulthint
    String errorMessage = "Register failed";

    if (text != null && text.isNotEmpty) {
      //match [[error:xxx]] ,Extractinerrorcontent
      final regExp = RegExp(r'\[\[error:(.*?)\]\]');
      final match = regExp.firstMatch(text);

      if (match != null) {
        //Extracttocustomerrorcontent
        errorMessage = match.group(1)!.trim();
      } else {
        //texterror,directuse
        errorMessage = text.trim();
      }
    }
    // Toast
    AppState.shared.showToast(errorMessage);
  }

  @override
  Widget build(BuildContext context) {
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      child: GestureDetector(
        onTap: () => FocusScope.of(context).unfocus(),
        behavior: .opaque,
        child: Column(
          children: [
            Expanded(
              child: CustomScrollView(
                slivers: [
                  CupertinoSliverNavigationBar(
                    largeTitle: Text("Register"),
                    padding: .only(start: 8, end: 8),
                    leading: CupertinoNavigationBarBackButton(
                      previousPageTitle: "Login",
                    ),
                  ),
                  SliverList.list(
                    children: [
                      CupertinoListSection.insetGrouped(
                        header: Text("Username"),
                        children: [
                          CupertinoListTile(
                            padding: .zero,
                            title: CupertinoTextField(
                              controller: usernameController,
                              placeholder: "username",
                              placeholderStyle: TextStyle(
                                fontSize: 20,
                                color: CupertinoColors.placeholderText
                                    .resolveFrom(context),
                              ),
                              style: TextStyle(
                                fontSize: 20,
                                color: CupertinoColors.label.resolveFrom(
                                  context,
                                ),
                              ),
                              keyboardType: .name,
                              padding: .only(
                                left: 20,
                                right: 20,
                                top: 15,
                                bottom: 15,
                              ),
                              decoration: BoxDecoration(),
                              autofocus: true,
                              textInputAction: .next,
                            ),
                          ),
                        ],
                      ),
                      CupertinoListSection.insetGrouped(
                        header: Text("Password"),
                        children: [
                          CupertinoListTile(
                            padding: .zero,
                            title: CupertinoTextField(
                              controller: passwordController,
                              placeholder: "password",
                              placeholderStyle: TextStyle(
                                fontSize: 20,
                                color: CupertinoColors.placeholderText
                                    .resolveFrom(context),
                              ),
                              style: TextStyle(
                                fontSize: 20,
                                color: CupertinoColors.label.resolveFrom(
                                  context,
                                ),
                              ),
                              keyboardType: .visiblePassword,
                              padding: .only(
                                left: 20,
                                right: 20,
                                top: 15,
                                bottom: 15,
                              ),
                              decoration: BoxDecoration(),
                              autofocus: true,
                              textInputAction: .next,
                            ),
                          ),
                        ],
                      ),
                      CupertinoListSection.insetGrouped(
                        header: Text("Email"),
                        children: [
                          CupertinoListTile(
                            padding: .zero,
                            title: CupertinoTextField(
                              controller: emailController,
                              placeholder: "email",
                              placeholderStyle: TextStyle(
                                fontSize: 20,
                                color: CupertinoColors.placeholderText
                                    .resolveFrom(context),
                              ),
                              style: TextStyle(
                                fontSize: 20,
                                color: CupertinoColors.label.resolveFrom(
                                  context,
                                ),
                              ),
                              keyboardType: .emailAddress,
                              padding: .only(
                                left: 20,
                                right: 20,
                                top: 15,
                                bottom: 15,
                              ),
                              decoration: BoxDecoration(),
                              autofocus: true,
                              textInputAction: .go,
                              onSubmitted: (value) {
                                if (mounted) {
                                  FocusScope.of(context).unfocus();
                                }
                                register();
                              },
                            ),
                          ),
                        ],
                      ),
                    ],
                  ),
                ],
              ),
            ),
            Padding(
              padding: .only(
                top: 15,
                left: 15,
                right: 15,
                bottom: 15 + MediaQuery.paddingOf(context).bottom,
              ),
              child: CupertinoButton.filled(
                sizeStyle: .large,
                child: SizedBox(
                  width: .infinity,
                  child: Center(child: Text("Submit")),
                ),
                onPressed: () {
                  if (mounted) {
                    FocusScope.of(context).unfocus();
                  }
                  register();
                },
              ),
            ),
          ],
        ),
      ),
    );
  }
}

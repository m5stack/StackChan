# Firmware Notes

## App とは何か

このファームウェアでの `App` は、StackChan 本体上で動く「画面つきの機能単位」です。

スマートフォンのアプリに近い考え方で、AI Agent、Avatar、Dance、Setup、ESP-NOW Remote などがそれぞれ独立した `App` として実装されています。

## コード上の位置づけ

- `firmware/main/main.cpp`
  - `GetMooncake().installApp(...)` で各 App を登録している。
  - メインループでは `GetMooncake().update()` が呼ばれ、登録された App 群が Mooncake フレームワーク経由で動く。
- `firmware/main/apps/apps.h`
  - 利用する App のヘッダをまとめて include している。
- `firmware/main/apps/app_template/`
  - 新しい App を作るための最小サンプル。

## App の基本構造

App は主に `mooncake::AppAbility` を継承した C++ クラスとして作る。

代表的なライフサイクル:

- `onCreate()`
  - App がインストールされたときに呼ばれる。
- `onOpen()`
  - App が開かれたときに呼ばれる。
  - UI 作成や初期化処理を書く場所。
- `onRunning()`
  - App 実行中に繰り返し呼ばれる。
  - 定期処理、センサー確認、状態更新などを書く場所。
- `onClose()`
  - App が閉じられたときに呼ばれる。
  - UI 破棄やリソース解放を書く場所。

## 既存 App

`firmware/main/main.cpp` では以下が登録されている。

- `AppLauncher`
- `AppAiAgent`
- `AppAvatar`
- `AppEspnowControl`
- `AppAppCenter`
- `AppEzdata`
- `AppDance`
- `AppSetup`

## 起動時に動く App

起動直後に最初に開かれる App は `AppLauncher`。

根拠:

- `firmware/main/main.cpp`
  - `GetMooncake().installApp(std::make_unique<AppLauncher>());` が最初に登録される。
- `firmware/main/apps/app_launcher/app_launcher.cpp`
  - `AppLauncher::onLauncherCreate()` の中で `open()` を呼んでいる。
  - そのため Launcher は作成直後に自分自身を開く。

起動後の分岐:

- まだアプリ設定が済んでいない場合
  - `GetHAL().isAppConfiged()` が `false`。
  - `AppLauncher` の中で `setup_workers::StartupWorker` が作られる。
  - 画面には `Welcome! Let's get started.` と `Skip` / `Start` が出る。
  - これは独立した App ではなく、Launcher 内で動く初期設定用ワーカー。
- アプリ設定が済んでいる場合
  - `create_launcher_view()` が呼ばれる。
  - アプリ一覧の Launcher 画面が表示される。

`isAppConfiged()` は `firmware/main/hal/hal_ble.cpp` にあり、NVS の `app_config` 名前空間にある `is_configed` フラグを見る。
Wi-Fi 設定が成功すると `is_configed = true` に保存される。

つまり「起動時にどの App が動いているか」という質問への答えは、基本的には `AppLauncher`。
ただし未設定の初回起動では、見た目としては Launcher の通常画面ではなく、Launcher 内の `StartupWorker` による初期設定画面が動いている。

## 最初に改造するなら

最初は `firmware/main/apps/app_template/` をコピーして、新しい App を追加するのが安全。

理由:

- HAL やサーボ制御を直接触らずに試せる。
- 画面表示、ボタン、定期処理の流れを小さく確認できる。
- 失敗しても既存の主要機能への影響が比較的小さい。

新しい App を追加する時に見る場所:

- `firmware/main/apps/app_template/app_template.h`
- `firmware/main/apps/app_template/app_template.cpp`
- `firmware/main/apps/apps.h`
- `firmware/main/main.cpp`

## 注意点

- LVGL の UI 操作時は `LvglLockGuard` または `GetHAL().lvglLock()` / `GetHAL().lvglUnlock()` を使う。
- サーボ、電源、OTA、ネットワーク初期化などは影響範囲が大きいので、最初の改造対象にはしない。
- まず無改造で `idf.py build` と実機への `flash` が通ることを確認する。

# Raising App Plan

## 作りたいもの

StackChan の中に「育成アプリ」を追加する。

最初のバージョンでは、複雑な AI や通信は入れず、以下だけに絞る。

- StackChan の顔を表示する
- なでる、あげる、あそぶ、休ませる、のような操作を用意する
- なつき度、元気、空腹度などの状態を持つ
- 状態を NVS に保存して、再起動後も続きから遊べるようにする

## なぜ App として作るか

既存ファームウェアでは、機能単位が `App` として分かれている。

育成機能も `AppRaise` のような新しい App として作るのが自然。

既存の流れ:

- `AppDance`
- `AppAvatar`
- `AppSetup`
- `AppAiAgent`

これらと同じように `firmware/main/apps/app_raise/` を追加する。

## 最初の実装方針

最初は `firmware/main/apps/app_template/` をコピーして作る。

想定ファイル:

- `firmware/main/apps/app_raise/app_raise.h`
- `firmware/main/apps/app_raise/app_raise.cpp`

登録する場所:

- `firmware/main/apps/apps.h`
- `firmware/main/main.cpp`

## 最小機能

### 画面

- 中央に StackChan の通常アバターを表示
- 下か横に小さな操作ボタンを置く
  - `Feed`
  - `Play`
  - `Pet`
  - `Rest`
- 画面上部または下部に状態を表示
  - `Love`
  - `Energy`
  - `Hungry`

### 状態

最初は整数 3 つで十分。

- `love`
  - なつき度
  - 0-100
- `energy`
  - 元気
  - 0-100
- `hunger`
  - 空腹度
  - 0-100

意味:

- `Feed`
  - `hunger` を下げる
  - 少し `love` を上げる
- `Play`
  - `love` を上げる
  - `energy` を下げる
  - `hunger` を上げる
- `Pet`
  - `love` を上げる
- `Rest`
  - `energy` を上げる

### 時間経過

`onRunning()` で一定時間ごとに状態を少し変える。

- 時間が経つと `hunger` が増える
- 時間が経つと `energy` が少し減る
- `hunger` が高すぎると `love` が少し下がる

最初はリアルタイム時計を厳密に使わず、アプリを開いている間だけ変化させる。
後で RTC を使って、閉じている間の経過も反映できるようにする。

## 保存

既存コードでは `Settings` が NVS 保存に使われている。

例:

- `firmware/main/hal/hal_ble.cpp`
  - `Settings settings("app_config", true);`
  - `settings.SetBool("is_configed", true);`
- `firmware/main/hal/hal_servo.cpp`
  - `settings.GetInt(...)`
  - `settings.SetInt(...)`

育成アプリでは名前空間を `raise_app` にする。

保存キー案:

- `love`
- `energy`
- `hunger`
- `last_update_ms`

## 表情・動き

最初は状態に応じて表情だけ変える。

- `love` が高い: happy
- `energy` が低い: tired 風
- `hunger` が高い: sad 風

既存の Avatar / Modifier を使えそう。
詳しくは以下を調査する。

- `firmware/main/stackchan/avatar/`
- `firmware/main/stackchan/modifiers/`
- `firmware/main/apps/app_avatar/app_avatar.cpp`

## 段階的な実装順

1. `AppRaise` を追加して Launcher に表示する
2. 開くと Avatar と Home indicator が出るようにする
3. `love` / `energy` / `hunger` をメモリ上だけで動かす
4. ボタン操作で状態を変える
5. `Settings` で NVS 保存する
6. 状態に応じて表情を変える
7. 余裕があればサーボの軽い動き、LED、効果音を追加する

## 注意点

- 最初からサーボを大きく動かさない。
- `onRunning()` で重い処理をしない。
- LVGL の UI 操作は `LvglLockGuard` の中で行う。
- 保存処理を毎フレーム行わない。操作時、または数秒おきに限定する。

## 実装メモ

2026-05-27 時点で、最初のプロトタイプを追加した。

追加ファイル:

- `firmware/main/apps/app_raise/app_raise.h`
- `firmware/main/apps/app_raise/app_raise.cpp`

変更ファイル:

- `firmware/main/apps/apps.h`
- `firmware/main/main.cpp`

実装済み:

- Launcher に `RAISE` App を追加
- `love` / `energy` / `hunger` の状態を追加
- `Feed` / `Play` / `Pet` / `Rest` ボタンを追加
- ボタン操作で状態を更新
- 状態を `Settings` 経由で NVS に保存
- 状態に応じて表情を変更
- 起動中は 15 秒ごとに空腹・元気を少し変化

未検証:

- この作業環境には `python` / `py` がなく、`fetch_repos.py` を実行できなかった。
- この作業環境には `idf.py` がなく、ESP-IDF ビルドを実行できなかった。

実機確認時に必要な作業:

```powershell
cd firmware
python .\fetch_repos.py
idf.py build
idf.py -p COMx flash monitor
```

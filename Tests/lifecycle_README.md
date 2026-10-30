# 起動・終了と設定反映のCIテスト

`lifecycle_test`は実際のApp::Init / Run / Deinit、Setting::Init、SDLのイベントキュー、
設定・自動stateのファイルI/Oを使います。RA有効ビルド内のRA mode OFFが対象です。

- 保存データなしでの初回起動と初期値。
- SDL_QUITをイベントキューへ投入し、実Run経由で終了時のstate0.binを保存。
- Deinitでの設定保存、次回Initでの設定反映、Run開始時の自動state復元。
- 自動復元後のInputへのカーソル→テンキー設定反映。
- CLIの4機種モード×3クロック指定の実行時反映。
- CLI起動のRun終了時には通常のstate0.binを上書きしないこと。
- CLI指定を終了保存へ残さず、明るさなど無関係な変更は保存すること。
- 起動ディスク不正、必要な起動ファイルなし、設定先の取得失敗・長すぎる設定先の拒否。
- 部分初期化・完全初期化の後始末と、Deinitの重複呼び出し、音声デバイスの解放。

## 隔離

テスト翻訳単位で、設定とEMU_SDLのSDL_GetPrefPathだけを一意なテストディレクトリへ
向けます。ROM探索先、RA DB、設定、自動stateがすべてその下に収まります。
ユーザーの保存先、資格情報、実ROMは使いません。

ファイルの有無を調べる初期化を通すため、一時領域にPC88.ROMとKANJI1.ROMという名前の
ゼロ埋めデータを生成します。これはROMのダンプではなく、実ゲームの動作検証には使いません。
通常起動はメニューで停止し、CLI起動もテストから先にメニューへ入れてCPUを停止したまま
終了イベントを処理します。生成ファイルはコミットせず、終了後にテストディレクトリを削除します。

画面はSDL dummy videoとsoftware rendererです。音声のOpen/Pause/Closeとエラーダイアログは
テスト用の同期アダプターへ置き換えます。AppやVMの保存・復元、設定初期化、描画処理は
置き換えていません。実画面や実デバイスの確認とは区別します。

設定先取得失敗のテストで確認したnull参照を避けるため、UI側Setting::Initは
SDL_GetPrefPathの失敗と長すぎる値を検査し、SDLから返されたパスを使用後に解放します。
コアと保存形式は変更していません。

```sh
scripts/verify_ra.sh --scope state
```

既存のmacOS・Windows x64・Linux x86_64/arm64 CTest CIに登録します。
OSの実表示・音、実ROM起動、RA mode ON、実入力機器・AndroidのActivity lifecycleは対象外です。

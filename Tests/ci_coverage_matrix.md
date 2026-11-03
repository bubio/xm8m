# CI・VM・実機の確認範囲

この表は段階的なCI拡充PRをすべて取り込んだ場合の構成を示す。ドラフトPRのCI成功と、mainへの取り込みは区別する。

## CIの実行範囲

| OS・CPU | 実行するもの | 実行環境・結果 |
|---|---|---|
| macOS ARM64 | 全CTest | macos-15、OS/CPU別JUnit・CTestログ |
| macOS Intel x86_64 | 全CTest | macos-15-intel、OS/CPU別JUnit・CTestログ |
| Windows x64 | 全CTest | windows-2022、OS/CPU別JUnit・CTestログ |
| Windows x86 | 全CTest | windows-2022の32ビットプロセス、OS/CPU別JUnit・CTestログ |
| Windows ARM64 | 全CTest | windows-11-arm、OS/CPU別JUnit・CTestログ |
| Linux x86_64 | 全CTest | 既存Linux x86_64ワークフロー、実行ログ |
| Linux ARM64 | 全CTest | 既存Linux arm64ワークフロー、実行ログ |
| Linux ARM32 armhf | 全CTest | クロスビルド後にQEMU user mode、JUnit・CTestログ |
| Android x86_64 / API29 | 設定・入力・音声・FILEIOの4 C++テスト | Androidエミュレーター、JUnit・終了コード・出力 |
| Android / Java | RaHttpHeadersTest | 既存Java単体テスト・JUnit |
| 受入記録ツール | Python単体テスト | 証跡の整合性確認。GUI受入の代替ではない |

デスクトップ系はRA有効でビルドし、通常動作テストは実行時RAモードOFFで行う。共通単体テストは既存と同じ依存範囲で実行する。macOS・Windowsは実テストバイナリのCPUをヘッダーで確認する。

全CTestには設定の保存・破損、入力の押下・解放・割り当て、音声リングバッファ、App状態保存・失敗、媒体とRAの既存テストが含まれる。以下の追加PR取り込み後は画面・メニュー、起動・終了、テープ・媒体I/O異常も自動登録される。OS固有テストがあるため、OS間で件数は異なる。

## 配布ビルドと実行確認の違い

Android APKは4 ABI（ARMv7・ARM64・x86・x86_64）を引き続きビルドするが、C++テストの実行は上表のx86_64のみ。macOS Universal、Windows配布物、Linux deb/rpm・AppImage・Flatpakの生成は、それぞれ既存ワークフローが担う。パッケージ生成成功を、そのパッケージ内でのGUI実行成功として扱わない。

ARM32のQEMUはARMユーザー空間の共通処理を確認する。Raspberry Pi実機のカーネルや周辺機器を再現するものではない。

## VM・実機に残す内容

| 層 | 確認する内容 |
|---|---|
| VM / E2E | 配布物の導入・起動・終了、画面とメニュー操作、OSのファイル選択や権限、実際の描画・入力・音声経路、保存・再起動後の復元 |
| 実機 | 実GPU・画面スケール、キーボード・タッチ・ゲームパッド、音声機器・遅延、Androidの回転・SAF・バックグラウンド復帰、Raspberry Piのデバイス固有動作 |
| ユーザー実受入 | 正規のROM・ゲーム・媒体を用いたRAや媒体の受入、実ネットワーク切断などの確認と証跡 |

単体テストではROMやユーザーの媒体を使用しない。生成データによるVMクラスの利用は、実際のエミュレーション操作の受入試験とは区別する。VMのスナップショットは再現可能な開始状態の準備に使えるが、テスト結果そのものの代わりにはならない。

## 段階とPR

| 段階 | 内容 | PR |
|---|---|---|
| 3 | 画面座標・メニュー・マウス・タッチ・キー操作 | [#94](https://github.com/bubio/xm8m/pull/94) |
| 4 | 起動・終了・自動保存復元・CLI一時設定・初期化失敗 | [#95](https://github.com/bubio/xm8m/pull/95) |
| 5 | テープ・状態復元・媒体I/O異常 | [#96](https://github.com/bubio/xm8m/pull/96) |
| 6a | Windows x86/ARM64・macOS Intelの全CTest実行 | [#97](https://github.com/bubio/xm8m/pull/97) |
| 6b | Android共通C++テスト実行 | [#98](https://github.com/bubio/xm8m/pull/98) |
| 6c | ARM32全CTest実行と範囲の整理 | [#99](https://github.com/bubio/xm8m/pull/99) |

各PRの最新CIと取り込み状態はリンク先で確認する。詳細な限界は各 `Tests/*README.md` に記載。コア `Source/ePC-8801MA/` の変更は含めない。

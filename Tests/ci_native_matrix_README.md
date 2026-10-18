# デスクトップCIの実行アーキテクチャ

`Existing_Tests.yml` はRA有効ビルドの全CTestを次の環境で実行する。

| OS | ランナー | 実行バイナリ |
|---|---|---|
| macOS | macos-15 | ARM64 |
| macOS | macos-15-intel | x86_64 |
| Windows | windows-2022 | x64 |
| Windows | windows-2022 | x86（64ビットOS上の32ビットプロセス） |
| Windows | windows-11-arm | ARM64（ARM64ホスト上） |

ビルド直後に実際のテスト実行ファイルのPE/Mach-Oヘッダーを読み、マトリクスとCPUが一致することを確認する。テスト結果とCTestログをOS・CPU別のartifactに保存する。設定・入力・音声・状態・RAテストなど、登録されている全テストが対象で、後続で登録されたテストも含まれる。

Linux x86_64/ARM64は既存の各 `Linux_*` ワークフローで全CTestを実行する。Windowsの配布用MSBuild、macOS Universalなどのパッケージビルドも継続する。

このPRは実行範囲の拡張のみで、製品ソースを変更しない。Androidの共通C++テスト、Raspberry Pi armhfの実行範囲は別段階。SDLの実GUI、OSのデバイス・権限、配布物の実環境での受入を確認したことにはならない。

ランナーの出典: [GitHub-hosted runners reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)。

# Raspberry Pi向けARM32 CI

既存の `RaspberryPiOS_armhf.yml` のクロスビルドでRA有効・BUILD_TESTING有効を明示し、全CTestをQEMU user modeで実行する。

- ARM32実行ファイルを `qemu-arm-static` で実行する。CMakeの実行エミュレーター設定とbinfmt登録を併用し、テスト内から起動されるARM実行ファイルにも対応する。
- 実行ファイル情報と全CTestの結果・ログをCIに記録する。
- 既存のdebパッケージ生成と法的文書の確認を継続する。
- コアは変更しない。ARM32で再現したディレクトリ列挙失敗に対し、RAのファイルユーティリティだけglibcの64ビットファイルAPIを選ぶ。ホストの大きなinode番号でreaddir/statが失敗する問題を防ぐ。

これはUbuntuのARM32ユーザー空間での共通処理の確認。Raspberry Piの実カーネル、実機固有のGPU・音声・入力、GUI操作、パッケージの実機受入を保証しない。

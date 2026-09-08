# xm8mac RetroAchievements対応仕様

## 1. この文書群の目的

このディレクトリは、xm8macへRetroAchievements（以下RA）対応を追加するための
実装仕様を定義する。実装担当者は、ここに明記された方針を独自判断で変更せず、
仕様変更が必要な場合は先に文書を更新すること。

本対応は、単にRA APIを呼び出す機能ではない。次の機能を一体として追加する。

- RA認証、ゲーム識別、実績評価、Leaderboard、Rich Presence
- 元のD88を変更しないRA専用媒体管理
- 複数ディスクとマルチイメージD88を扱うゲームライブラリ
- Windows、macOS、Linux、Androidで共通動作するSDLオーバーレイUI
- SoftcoreとHardcoreの実行制約

### 1.1 対象コードと系譜

本仕様の実装対象は、このリポジトリにあるxm8macである。コードの系譜は次のように
扱う。

```text
ePC-8801MAの旧版
  -> XM8（xm8macが追跡対象とする移植元）
    -> xm8mac（本仕様の実装対象）
```

- XM8はePC-8801MAのかなり古い版から派生しており、現在のePC-8801MAとの
  ソースマージは前提にしない。
- RA対応で追跡を意識する移植元はXM8である。ePC-8801MAの現在版を上流とは
  みなさない。
- XM8の更新が長期間ないことは、既存コアを無制限に変更する理由にはしない。
  コア変更を抑える目的は、将来XM8側の修正を比較・移植しやすくすることと、
  エミュレーション回帰の範囲を限定することにある。
- `Source/ePC-8801MA/`というディレクトリ名は歴史的な構成であり、その内容を
  現在のePC-8801MAへ同期することを意味しない。

### 1.2 文書の規範性

- 本READMEは文書群全体の入口、確定方針、優先順位、完了条件を定義する。
- 詳細なデータ形式、API呼出し順、画面挙動、テスト手順は各分割文書を規範とする。
- READMEと分割文書が矛盾した場合、実装者は推測で解決しない。実装を止め、
  両方の文書を同じ変更で整合させてから再開する。
- 外部サービスや依存ライブラリの仕様変更により実装不能になった場合も、暗黙に
  代替仕様へ変更せず、根拠と影響範囲を文書化して承認を得る。

## 2. 文書構成

| 文書 | 内容 |
|---|---|
| [01_要件とアーキテクチャ.md](01_要件とアーキテクチャ.md) | モード、コンポーネント、`rc_client`、通信、メモリ、スレッド |
| [02_ゲームライブラリとD88保存仕様.md](02_ゲームライブラリとD88保存仕様.md) | SQLite、D88ハッシュ、作業コピー、M3U、複数ディスク、SAF |
| [03_オーバーレイUI仕様.md](03_オーバーレイUI仕様.md) | 画面、入力、通知、バッジ、レイアウト、アクセシビリティ |
| [04_Hardcoreと状態管理.md](04_Hardcoreと状態管理.md) | Hardcore制約、ステート、速度、一時停止、バックグラウンド |
| [05_実装工程とテスト計画.md](05_実装工程とテスト計画.md) | 実装順序、コミット境界、テスト、受入条件 |
| [06_外部仕様と依存関係.md](06_外部仕様と依存関係.md) | 依存物、固定バージョン、ライセンス、更新手順、公式資料 |
| [07_Phase0実施記録.md](07_Phase0実施記録.md) | macOS基準build/test、Normal実動作、fixture、後続OS build調査 |
| [08_Phase1実施記録.md](08_Phase1実施記録.md) | macOS RA依存物、ON/OFF build、ライセンス監査、検証結果 |
| [09_Phase2実施記録.md](09_Phase2実施記録.md) | macOS RAメモリ検査API、D88 hash/media probe、検証結果 |
| [10_Phase3実施記録.md](10_Phase3実施記録.md) | macOS SQLiteライブラリ、D88作業コピー、M3U起動構成、検証結果 |
| [11_Phase4実施記録.md](11_Phase4実施記録.md) | macOS HTTP、認証、RAセッション開始、トップ階層RAメニュー、検証結果 |
| [12_Phase5実施記録.md](12_Phase5実施記録.md) | macOS RAオーバーレイ基盤、通知状態分離、検証結果 |
| [13_実ROMなしLibrary検証手順.md](13_実ROMなしLibrary検証手順.md) | 生成fixtureを使うLibrary v1手動確認、seed tool、検証結果 |
| [14_Phase5-6継続実施記録.md](14_Phase5-6継続実施記録.md) | 現状再監査、実フレーム評価、ASCEND媒体検査、Phase 5-6残件 |
| [15_Phase5媒体交換実施記録.md](15_Phase5媒体交換実施記録.md) | 同一ゲーム媒体交換、VM失敗時rollback、整合不能時Offline遷移、次回handoff |
| [16_Phase5Offline状態機械実施記録.md](16_Phase5Offline状態機械実施記録.md) | 起動失敗時Offline、切断・再接続、途中復帰禁止、次回Library同期handoff |
| [17_Phase5Library同期実施記録.md](17_Phase5Library同期実施記録.md) | bank別hash、hash/title/progress同期、transaction、schema v2、次回handoff |
| [18_Phase5実VMフレーム計数実施記録.md](18_Phase5実VMフレーム計数実施記録.md) | 実PC-8801MA VMによる通常、複数frame、描画skip、Full Speed相当のcallback計数 |
| [19_Phase5媒体競合起動制限実施記録.md](19_Phase5媒体競合起動制限実施記録.md) | 競合ゲームのLibrary表示、Game Detail表示、UI／DB二重の起動禁止 |
| [20_Phase5媒体競合解消実施記録.md](20_Phase5媒体競合解消実施記録.md) | 競合分類、明示MERGE／SPLIT、MANUAL拒否、transaction rollback検証 |
| [21_Phase5接続監視修正実施記録.md](21_Phase5接続監視修正実施記録.md) | macOS到達性監視、即時切断・復旧、重複通知抑止、手動再確認手順 |
| [22_Phase5完了判定.md](22_Phase5完了判定.md) | Phase 5受入条件、実HTTPS・ASCEND・切断復旧確認、Phase 6 handoff |
| [23_Phase6通知Queue実施記録.md](23_Phase6通知Queue実施記録.md) | 優先度付き通知、同時1件、待機・中断再開、設定寿命、queue上限 |
| [24_Phase6画像cache実施記録.md](24_Phase6画像cache実施記録.md) | badge永続cache、デコード検証、LRU容量制御、破損自己修復、placeholder |
| [25_Phase6入力導線実施記録.md](25_Phase6入力導線実施記録.md) | keyboard、mouse、controller、touch、page移動、pointer安全化、Android Login入力方針 |
| [26_Phase6動的ステータス行表示計画.md](26_Phase6動的ステータス行表示計画.md) | 通知、Challenge、Progress、Leaderboard trackerの排他的なステータス行表示と検証計画 |
| [27_Phase6動的ステータス行表示実施記録.md](27_Phase6動的ステータス行表示実施記録.md) | 全幅ステータス行、単一通知、3秒page切替、入力透過、自動検証、ASCEND手動受入 |
| [28_RA設定簡素化方針.md](28_RA設定簡素化方針.md) | 独立Settings画面を設けず、RA modeとSoftcore／Hardcoreだけを公開する方針 |
| [29_Phase6安全文字変換実施記録.md](29_Phase6安全文字変換実施記録.md) | UTF-8厳密検証、Shift-JIS安全化、文字境界・表示幅計算、既存KANJI ROM描画への接続 |
| [30_Phase6完了監査.md](30_Phase6完了監査.md) | Phase 6仕様突合、送信エラー1件保持、最終自動検証、視覚受入とPhase 7 handoff |
| [31_Phase7RA_Casualステート実施記録.md](31_Phase7RA_Casualステート実施記録.md) | RA Casual／Offline専用state、進捗同期、事前検証、rollback、手動受入 |
| [32_Phase7コードレビューとリファクタリング.md](32_Phase7コードレビューとリファクタリング.md) | Hardcore着手前のstate境界、I/O失敗処理、公開API、異常系試験の監査 |
| [33_RAステートメニュー分離実施記録.md](33_RAステートメニュー分離実施記録.md) | Normal／RAのLoad・Save導線分離、利用不可通知、メニューID衝突修正 |
| [34_Phase8Hardcore実施記録.md](34_Phase8Hardcore実施記録.md) | macOS Hardcore中央policy、操作制限、非停止UI、RESET、検証と手動受入 |
| [35_Hardcore初期値とメインメニューRA表示.md](35_Hardcore初期値とメインメニューRA表示.md) | 新規環境のHardcore初期選択、既存設定維持、メインメニューのRA状態表示 |
| [36_macOS総合回帰修正記録.md](36_macOS総合回帰修正記録.md) | RA起動時の保存済みInput remap適用漏れと回帰確認 |
| [37_Phase9Windows移植計画.md](37_Phase9Windows移植計画.md) | Windows事前監査、UTF-8 path、WinHTTP、Visual Studio構成、試験matrix、受入計画 |
| [38_Phase9Windows実施記録.md](38_Phase9Windows実施記録.md) | Phase 9完了記録、Windows移植・自動試験、Windows／ARM64実機受入、macOS回帰、環境固有の未実施項目 |
| [39_Phase10Linux実施記録.md](39_Phase10Linux実施記録.md) | Linux libcurl multi、接続監視、Secret Service、CMake／配布、Linux自動受入、実機受入項目 |
| [40_Phase11Android実施記録.md](40_Phase11Android実施記録.md) | Android Keystore、HTTPS、SAF、build経路、実機受入残件 |
| [41_Hardcore完全準拠開発計画.md](41_Hardcore完全準拠開発計画.md) | 公式A〜G対応、実装・試験・証跡・外部承認の計画と状態 |
| [42_Hardcore完全準拠検証記録.md](42_Hardcore完全準拠検証記録.md) | 自動・OS・実通信・配布物の検証結果と申請履歴 |
| [45_RA統一媒体状態仕様と検証基準.md](45_RA統一媒体状態仕様と検証基準.md) | **現行規範**。RA ONの統一媒体状態機械、通信断・outbox、Hardcore禁止操作、必須自動試験 |
| [46_RA統一媒体動作確認運用手順.md](46_RA統一媒体動作確認運用手順.md) | **現行運用規範**。45の自動・GUI・ユーザー実受入手順 |
| [47_RA媒体調停内部設計案.md](47_RA媒体調停内部設計案.md) | **設計案**。45の動作を維持する媒体調停の共通化、テスト境界、段階的移行 |
| [48_RA媒体状態機械詳細設計.md](48_RA媒体状態機械詳細設計.md) | **詳細設計案・引継ぎ資料**。状態×イベントの遷移定義、生成Mermaid図、非同期処理・reset・復元の契約と検証方法 |
| [49_RA状態機械置換前の回帰基準.md](49_RA状態機械置換前の回帰基準.md) | **置換前の基準**。本番App・実FDCを通す回帰試験、検証結果、push後の別ブランチ移行 |
| [50_RA媒体変更の中間実媒体受入.md](50_RA媒体変更の中間実媒体受入.md) | **中間受入カード**。検証対象build、ユーザーによる実媒体・GUI確認と報告方法 |
| [43_RAディスクマウント状態機械再設計計画.md](43_RAディスクマウント状態機械再設計計画.md) | **廃止**。旧媒体状態機械の履歴資料。仕様・試験の根拠に使用しない |
| [44_動作確認運用手順.md](44_動作確認運用手順.md) | **廃止**。旧運用記録。仕様・試験の根拠に使用しない |

## 3. 確定事項

### 3.1 モード

- 既存動作を`Normal`と呼ぶ。`Normal`ではD88、ステート、速度、メニューの
  現行仕様を変えない。
- RA機能は既定OFFとする。有効時だけ`Softcore`または`Hardcore`で動作する。
- RA有効/無効と最後に選択した`Softcore`/`Hardcore`は別々に保存する。初回の
  RAモードは`Hardcore`とし、RAをOFFにしても最後の選択を失わない。
- RA設定は既存`setting.bin`へ追加せず、`ra/library.sqlite3`へ保存する。
- RAモードの変更は現在のゲームセッションを終了してから適用する。

### 3.2 D88識別

- PC-8000/8800のRA識別値は、起動に使うD88イメージ1枚分のMD5
  32文字小文字16進表記とする。
- 単一イメージD88ではファイル全体MD5がRA識別値になる。
- マルチイメージD88では、選択中bankのD88イメージ範囲だけをRA識別値に使う。
  ファイル全体MD5は保存管理用の媒体IDとしてのみ使う。
- M3Uの初回ゲーム識別には先頭の有効なD88を使う。
- D88データ自体をRAサーバーへ送信しない。

### 3.3 D88の保存

- RAモードでは元D88を直接開かない。原本から作成したアプリ専用の
  `working.d88`だけをVMへ渡す。
- 同じMD5の媒体は同じ作業コピーを再利用する。
- 原本の内容が変わった場合は別媒体として登録する。作業コピーを自動移行しない。
- `Normal`では従来どおりユーザーが指定したD88を直接扱う。

### 3.4 UIとプラットフォーム

- アプリ固有UIはすべて640x400論理解像度のSDLオーバーレイとして描画する。
- OS権限画面とAndroid Storage Access Framework（SAF）の文書・ツリー選択だけを
  例外とする。RA専用のActivity、Window、ネイティブダイアログは作らない。
- 最終的にWindows、macOS、Linux、Androidを同じ機能範囲でサポートする。
  これは最終到達点であり、4 OSを同時に実装することを意味しない。
- 実装と受入は`macOS -> Windows -> Linux -> Android`の順で直列に進める。
- Androidの通常build最低APIは既存方針どおりAPI 19、検証上限はAPI 36とする。
  RetroAchievements有効buildは安全なtoken保存のためAPI 23以上を必須とし、
  API 23未満ではRetroAchievements自体を無効にする。

### 3.5 オフライン

- 認証、ゲーム識別、セッション開始のいずれかに失敗してもゲーム起動は継続する。
- 失敗したセッションではRA評価と送信を完全に無効化し、画面に明示する。
- ゲーム実行途中で接続が戻っても、そのセッションではRAを再開しない。
- RAを再開するにはゲームを閉じ、RAセッションを最初から開始する。
- `Hardcore`を要求していても開始処理に失敗したセッションはHardcoreとして扱わない。
  `Offline session`を明示し、Normal相当の操作を許可する。
- DB、作業コピー、原本検証などローカル媒体処理の失敗はオフライン継続の対象外とする。
  原本を直接開くfallbackは禁止し、RAモードでの起動を中止する。

### 3.6 RA統合方式

- `rcheevos` v12.3.0を固定版でソース同梱し、低水準実績APIではなく
  `rc_client`を使用する。
- D88のハッシュだけでなく、ログイン、ゲームロード、実績、Leaderboard、
  Rich Presence、Hardcore判定も`rc_client`の契約に従う。
- 通信実装はプラットフォーム別とするが、RAセッションの状態遷移とエラー方針は
  全プラットフォームで共通にする。
- RAのゲーム識別結果を永続ライブラリへ保存しても、次回起動時の
  `rc_client`によるゲームロードを省略しない。

## 4. 用語

| 用語 | 定義 |
|---|---|
| 原本 | ユーザーが管理するD88。RAモードでは読み取り専用で参照する |
| 媒体 | MD5で識別される1つのD88ファイル |
| bank | 1つのD88に格納された内部ディスクイメージの番号 |
| 作業コピー | RAモード中にVMが読み書きする`working.d88` |
| ゲーム | 1つ以上の媒体と起動構成を束ねるライブラリエントリ |
| 起動構成 | Drive 1/2へ割り当てる媒体とbank、およびRA識別基準媒体 |
| RA active media | `rc_client`が現在のRAセッションで基準としている媒体 |
| RAセッション | ゲーム識別成功からアンロードまでの`rc_client`実行単位 |
| オフラインセッション | RA開始に失敗し、RA評価なしで継続しているゲーム実行 |
| XM8 | xm8macが派生元として追跡する移植元 |
| エミュレーションコア | VM、PC-88デバイス、CPU、FDC、D88処理など既存の実行系 |

## 5. 実装原則

1. RA対応コードを既存VMへ拡散させず、UI層のサービスとして分離する。
2. VMへ追加するRA専用APIは、副作用のない物理メモリ読み出しに限定する。
3. D88のパス変換はVMより前で完了し、VMは原本と作業コピーを区別しない。
4. `rc_client`を唯一のRAランタイムとし、実績条件評価を独自実装しない。
5. ネットワーク結果はメインスレッドへ配送し、通信スレッドからVMやUIを触らない。
6. DB更新と媒体配置はクラッシュ後も中間状態を残さない順序で行う。
7. 認証情報、D88内容、ユーザーのローカルパスをログへ出力しない。
8. 新機能は`XM8_ENABLE_RETROACHIEVEMENTS`で完全に除外可能にする。
9. 新規RA実装は原則として`Source/RA/`へ置き、既存UIとコアには接続点だけを追加する。
10. コアへ必要な接続点は、まず既存の公開APIまたは外側のアダプタで実現できないか
    確認する。実現できない場合だけ、最小の汎用APIを追加する。
11. コア内へRAの認証、HTTP、SQLite、画像、オーバーレイ、モード判定を持ち込まない。
12. RA無効buildでは、既存処理の制御フロー、データ保存形式、外部依存関係を
    意図せず変更しない。
13. 各OSはRA全機能、Normal回帰、RA無効buildの検証が完了するまで次のOSへ進まない。
14. 後続OS対応で共通コードを変更した場合は、そのOSだけでなく対応済みの全OSで
    回帰試験を再実施する。
15. 未着手OSではRAを有効化せず、既存Normal buildを維持する。後続OS向けstubや
    未検証buildを前工程の完了条件に含めない。
16. 共通コードの変更とOS固有移植はコミットを分離する。
17. `rc_client_do_frame()`は描画回数や音声buffer生成回数ではなく、実際に完了した
    VMフレームごとに1回呼ぶ。
18. RA設定とRA stateは既存`setting.bin`およびNormal stateから分離し、RA無効buildが
    既存形式を読み書きできる状態を維持する。
19. RAモードではRAメタデータを持たない旧stateをロードしない。旧stateはNormalでのみ
    読込可能とし、原本D88への書戻しや別ゲーム状態の混入を防ぐ。

## 6. 実装順序

実装は次の順序を変更しない。macOSを挙動上の基準実装とし、その全機能と受入試験を
完了してから、後続OSへ1つずつ移植する。

1. macOS基準線
2. macOS依存物・ビルド
3. macOSで共通RAコア、ライブラリ、HTTP、UI、Softcore、Hardcoreを完成
4. macOS受入
5. Windows移植・受入
6. Linux移植・受入
7. Android移植、SAF・タッチ・ライフサイクル対応・受入
8. 全OS最終整合試験

macOS版は挙動確認の基準であり、仕様の代わりではない。後続OSで実現困難な挙動が
見つかっても、macOS固有動作を暗黙に共通仕様へ昇格させない。

詳細な作業単位と完了条件は
[05_実装工程とテスト計画.md](05_実装工程とテスト計画.md)を参照すること。

## 7. 対象外

初期RA対応では次を実装しない。

- 現在のePC-8801MAからのコード移植または同期
- XM8コアの全面的な再設計、置換、別ライブラリ化
- RA Webサイト、RetroArch、外部ランチャーへの実行委譲
- 実績定義、コードノート、メモリ調査を行う開発者ツール
- D88以外の媒体形式へのRA対応
- D88本体、作業コピー、セーブデータのクラウド同期

対象外の機能を将来追加する場合は、本対応へ暗黙に混在させず、別の仕様変更として
依存関係と受入条件を定義する。

## 8. 実装開始前の必須確認

実装担当者はPhase 0で次を記録する。

1. 作業開始commitと、比較可能な場合は最後に取り込まれたXM8版または由来情報
2. `Source/ePC-8801MA/`と既存UIのうち、RA対応で変更する予定の接続点
3. macOSのRA有効・無効build command、および後続OSの既存Normal build構成
4. NormalのD88書込み、M3U、bank切替、ステート、Full Speedの基準動作
5. 著作物を含まないD88テストfixtureの生成方法

由来情報を特定できない場合は推測した版番号を記録せず、「特定不能」と根拠を残す。
由来情報の不明はRA実装を妨げないが、現在のePC-8801MAを比較基準に置き換えては
ならない。

## 9. 文書上の完了条件

本仕様書群は、次のすべてを満たす実装だけを「RA対応完了」と扱う。

- [05_実装工程とテスト計画.md](05_実装工程とテスト計画.md)の全Phaseと
  最終受入条件を満たしている。
- Windows、macOS、Linux、Androidで同一D88から同一RA Game IDを取得できる。
- Normalの既存機能に回帰がなく、RA無効buildが成立する。
- RAモードの起動前後で原本D88のMD5が変化しない。
- Softcore、Hardcore、オフラインセッションの制約に迂回経路がない。
- D88内容、パスワード、トークン、ローカル絶対パスを送信またはログ出力しない。
- 各Phaseの完了commit、テスト結果、未実施項目、承認済み差異が記録されている。

一部プラットフォーム、Leaderboard、Hardcore、ライブラリUIなどを省いた状態は、
試験buildまたは途中Phaseであり、本仕様の完了とはみなさない。

## 10. 要件トレーサビリティ

| 要件 | 詳細仕様 | 主な実装・受入Phase |
|---|---|---|
| Normal無回帰、RA既定OFF | 01、06 | Phase 0、1、8、9、10、11、12 |
| D88 hash、原本保護、作業コピー | 02 | Phase 2、3、各OS受入 |
| M3U、multi-bank、複数媒体 | 02 | Phase 3、5、各OS受入 |
| 認証、HTTP、Offline session | 01、06 | Phase 4、5、各OS HTTP適合試験 |
| 実フレーム評価、memory map | 01 | Phase 2、5 |
| ライブラリ同期、進捗、画像 | 02、03 | Phase 3、5、6 |
| SDLオーバーレイ、全入力方式 | 03 | Phase 6、各OS受入 |
| RA Casual state | 04 | Phase 7 |
| Hardcore制約 | 04 | Phase 8、各OS受入 |
| Windows移植 | 05、06 | Phase 9 |
| Linux移植 | 05、06 | Phase 10 |
| Android SAF・touch・lifecycle | 02、03、06 | Phase 11 |
| 4 OS同一Game IDと最終整合 | 05 | Phase 12 |

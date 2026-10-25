# ローカル専用E2E

CI・公開処理から独立した、任意実行のGUIテスト。製品・コアのソースは変更しない。

| 層 | 確認するもの |
|---|---|
| CI | ビルド、単体・結合テスト、異常注入による再送・状態遷移 |
| このローカルE2E | 選んだ配布物の画面と操作、RAオンライン、ゲストだけの切断・復帰 |
| 実機 | 実GPU、音声品質、実入力機器、Android・Pi固有の挙動 |

Hubへ入口を登録しただけでは各環境の校正は完了しない。未校正のケースは保留のままにする。

## 実行

```sh
python3 scripts/e2e.py list
python3 scripts/e2e.py doctor --target ubuntu
python3 scripts/e2e.py run --target ubuntu --suite basic --artifact /absolute/candidate.deb
python3 scripts/e2e.py run --target ubuntu --suite basic --case basic.menu --artifact /absolute/candidate.deb
python3 scripts/e2e.py run --target ubuntu --suite ra
python3 scripts/e2e.py run --target ubuntu --suite ra-network
```

終了コードはPASS/対象外=0、FAIL=1、PENDING=2。実行ごとに新しいrunとSHA-256を記録する。
`artifacts/local-e2e/config.json` を個人設定とし、`runs/` にJSON・Markdown・画面証跡を保存する。
このディレクトリ全体はGit除外。認証情報・BIOS・媒体・VMディスクをレポートへ含めない。
実績解除・実サーバーのoutbox受入を行ったことにはならない。

## 初回準備

1. `config.example.json` を上記の個人設定へコピーする。
2. 専用VMを用意し、VMPal Tools、GUIログイン、ランタイム、BIOS、テスト用媒体コピーを準備する。
   通常操作はRA有効build内のRAモードOFF。RA用の初回ログインはユーザーが行う。
   資格情報が失効・保護ダイアログが表示された場合はPENDINGにする。
3. アプリを正常終了し、VMを正常シャットダウンする。個人設定へこの**専用ベースVM**のUUIDを登録する。
4. `install` はguest内でcandidateを展開するargvの配列、`launch` はGUIセッションで起動するargv。
   Linuxでは`systemd-run --user`を使い、VMPalのexecジョブとGUIアプリの寿命を分離する。
   macOSはDMG等をguest内で展開、WindowsはZIP等をguest内で展開する。ホストで製品を実行しない。
5. 各ケースを実画面で校正し、明示的な操作と期待文字列を保存する。未校正のケースはPENDING。
   Linuxでは`ready`にGUIセッションの準備完了を確認するargvを指定できる（最長45秒）。
   `startup`に起動直後の画面準備を記述する。ここにも明示的な画面期待値が必要。

VMPalだけを使う場合も、AVDの同時起動を検出するためAndroid SDKの`adb`が必要。
個人設定のルートに`adb`の絶対パスを指定する。検出できなければVMを起動せず保留にする。
ユーザーがE2E用と指定した既存VMをOSごとに1台だけ使う。
`vm_id`と`vm_name`を既存VMの正確なID・名前に固定し、`dedicated_vm:true`、
`baseline_snapshot`へそのVMの基準snapshot UUIDを指定する。名前の変更は不要。
分類別の上書きでVMや基準snapshotを変更することは禁止する。

開始時は停止中の同じVMを基準へ戻し、指定した配布物を導入する。
実行ごとのクローン、snapshot追加、VM削除は行わない。
終了時はアプリを停止し、`snapshot_ready`の読み取り検査で未送信RA実績がないことを確認してVMを正常停止する。
未送信データ・検査失敗・異常停止があれば`vm-state/<UUID>.json`に復元不可を記録し、
次回はsnapshotへ戻さず保留にする。RA DBやoutboxを自動削除して解除しない。
`stop_app`と`snapshot_ready`は各OSのguest argvで用意する。

基本操作とRAは必要に応じてアプリのデータ保存先だけを分ける。
Ubuntuでは基本操作を専用XDG_DATA_HOMEで実行し、RAの資格情報を持たないテスト用設定を使う。
RAは保護されたKeyringを維持する。`credential_ready`へ解除状態のみを確認するargvを指定し、
`manual_credential_unlock:true`の場合だけ、実際にOSの解除画面を観測してから手動操作を依頼する。
解除の初期準備・認証失効時はユーザーがVM内で操作し、自動入力しない。
待機情報は`preparation.json`。解除時READY、終了時`waiting:false`・`vm_stopped:true`となり、古い依頼を無効にする。
`manual_unlock_timeout`は既定600秒、30〜900秒。キャンセル・時間切れは保留にする。
RA用データを分ける場合は、各分類の`launch`にもRA用の保存先を指定する。
RAレシピ例の`/home/USER/`は実際のゲスト内パスへ置き換える。画面のフォルダー名との照合にも使うため、環境変数のままにしない。

VM/AVDの同時稼働は合計1台。稼働中の別VM/AVDを停止しない。
共有フォルダー・clipboard・drag and drop・microphone・転送ポート・追加ローカルネットワークを使わず、
設定で指定したE2E VMと基準snapshotだけを操作する。基準の更新は自動で行わない。
試験結果はホストへ保存してsnapshot復元の対象から外す。

## レシピ

`when_exact_line:true`付きのクリック・キー操作は、`text`と完全一致する画面行がある場合だけ実行する。
例えばファイル選択画面の現在のフォルダーを条件にして移動できる。
省略した操作も記録し、移動後には媒体名などの明示的な期待値を置く。
条件に一致しないことだけでケースをPASSにしない。

例は説明用で、そのまま校正済みとして使用しない。

```json
{
  "calibrated": false,
  "steps": [
    {"action":"expect", "text":"Drive 1"},
    {"action":"click", "text":"System Options"},
    {"action":"expect", "text":"System Options"},
    {"action":"key", "keys":"Escape"},
    {"action":"expect", "text":"Drive 1"},
    {"action":"screenshot"}
  ]
}
```

`expect` は可視テキストを検証し、`absent:true` で非表示を検証する。1回の待機は最長45秒。
`pending_if`に認証失効などの表示を列挙でき、一致時は証跡を保存する前に保留にする。
保護されたログイン・Keyring画面も保留にし、自動入力しない。
RA分類では、可視テキスト全文を保存せず、レシピの期待文字列と実際の一致・不一致だけを記録する。
期待文字列へアカウント名・秘密情報を含めない。自動の失敗スクリーンショットも保存しない。
RAの画面証跡には`capture_region:[x0,y0,x1,y1]`を必須とし、アカウント情報が入らない領域を実画面で校正する。
画像比較にも`capture_region`を使う場合、`region`は切り出し後のPNG内の座標を指定する。
AndroidのRA画像記録は、この領域制限を校正するまで保留にする。
`launch`は個人設定の起動argvを呼び直す。`wait`は起動・アニメーション用の最長5秒の待機であり、合格条件にはならない。
`ensure_menu`は指定文字列がなければF11を送り、観測してから再試行する。既に見えるメニューを閉じない。
`fullscreen`はLinuxのウィンドウ情報とメニューから全画面化を確認する。
`screen_size`には校正時の実PNG寸法を指定する。領域付きの文字判定では寸法とフォーカスを確認する。
`hold`はキーを短時間保持してから解放する。BASIC入力など、短すぎるキーイベントを読み落とす場面で使う。
`expect`の`exact_line:true`は出力行を完全一致で確認し、入力したコマンドの表示だけで合格することを防ぐ。
`prepare_focus`は`focus_helper`で専用の別ウィンドウを用意し、`focus_cycle`はキーを押しながら
別ウィンドウへ移動・解放・復帰した実際のフォーカス変化を記録する。Ubuntu以外は校正が必要。
OSごとのキー表記を使う（VMPalは`Return`など、Androidは`KEYCODE_ENTER`など）。

OCRが安定しない文字には、校正した画像の固定領域を使う。

```json
{"action":"expect_image", "reference":"/absolute/local/baselines/mode-8.png",
 "region":[1120,768,1270,798], "max_changed_fraction":0.001}
```

座標は**保存されたPNGの左上原点のピクセル**で、画面操作APIの縮小表示座標とは区別する。
実画像と基準画像の寸法が異なる場合は比較を通さない。背景だけで特徴がない基準領域も原則保留にする。
取り外し後の空の媒体欄などは`allow_blank:true`を明示し、同じケース内で可視文字の肯定判定も必須とする。
媒体の挿入待ち・アクセス中の背景色を基準にせず、操作完了後の安定した表示を登録する。
基準画像・SHA-256・領域・差分率を記録する。macOSのSwift/AppKitでローカル比較し、外部へ送信しない。
基準画像の登録前に正しい状態と異なる状態の両方で、判定が変わることを確認する。
`different:true`では同じ領域の差分を期待する。状態を変更した後に差分を確認し、復元後に一致を確認する場合に使う。
`ubuntu-recipes.example.json`は基本操作7ケースの校正開始用の例。全ケースを未校正として配布する。
BIOS起動先、8MHz、両ドライブ空、BASIC入力待ちを基準状態とし、`E2E/E2E1.d88`と`E2E2.d88`に
DISK_A・DISK_B・USER_DISKの3バンクを持つテスト用コピーを準備する。
媒体名・表示言語・画面寸法が異なる環境ではレシピとローカル基準画像を校正し直す。

`guest` は `argv` をゲスト内だけで実行する。認証入力・秘密を出力するコマンドをレシピに書かない。
`network` の `state:down/up` はゲスト限定の切断・復帰。プロセス終了コードだけではケースをPASSにしない。
`network.*` は各ケースのレシピ内で新しいオンラインsessionを用意し、切断前・切断中・復帰後を
それぞれ観測する。`online_setup`に新しいsessionを作る操作とオンライン確認を記述する。
通常は通信分類のケース間で同じVM/AVDを停止し、同じ基準snapshotへ戻す。
手動Keyring解除を使う場合は実行中の同じE2E VMを維持し、ケース間で
`session_restart`の校正済みレシピによる正常終了・再起動を必須とする。
LinuxのPIDとプロセス開始時刻を比較し、同じプロセスのままならFAIL。解除済みのOS保管庫だけを維持し、
各ケースの`online_setup`で新しいオンラインsessionを用意する。RA DBやoutboxを巻き戻し・削除しない。
明示的なdown/upの組、切断中の期待値、復帰後の期待値が欠けたレシピは保留。
Drive 2の検証済みhash cacheはsession限定であり、基準snapshotから流用しない。
reset回数や内部active hashなど画面で観測できないチェックをこのレシピのPASSに含めない。

`ra_observation`を設定したLinux VMでは、切断前と後始末後にRA表示とoutbox件数を記録する。
`database`は専用環境の`library.sqlite3`、`status_region`は校正したRAステータス領域、
`game_text`は識別完了時の期待文字列にする。`observe_ra`ステップの`phase`で復帰直後や識別前後も記録できる。
RAメニューを開いて観測し、認証表示、識別表示、未送信・保留件数を分けて確認する。
段階名は画面ラベルの分類であり、内部状態の証明ではない。合計件数が残っていても、
それだけで現在の識別を妨げているとは推測しない。
DBは読み取り専用で集計し、アカウント名・実績ID・資格情報・生のDB内容は記録しない。
観測できない状態は`unobserved`、必須観測が取得できない実行はPENDINGとする。
観測失敗は取得段階と例外種別だけを保存し、秘密を含み得る例外本文は保存しない。
Ubuntuの通信レシピ例は`require_desktop_tools:true`で、VMPalのデスクトップ観測経路が失われたら
次の操作前に保留する。古い画面のOCRを操作成功として使わない。
件数が0でもサーバー受入成功とは判定しない。未送信レコードの発生・消失と、実サーバーでの受入確認は区別する。

## 通信試験

`network.calibrated` は、専用環境で復帰ヘルパーと操作チャネルの保持を確認した後だけtrueにする。
Linuxはrootのsystemd timer、macOSはguestのlaunchd、WindowsはSYSTEMのScheduled Taskで、
クライアント終了と独立した120秒の復帰処理を先に登録する。アダプター変更にはguest内の管理者実行を使う。
Androidは対象serialだけのWi-Fi・mobile dataを変更し、元状態を保存して復帰する。
ホストのNIC、firewall、hosts、VPNへは触れない。
切断中の観測は90秒以内に限定し、120秒の独立復帰後を「切断中」と判定しない。

通常・失敗・キャンセル時に復帰を試みる。復帰に失敗した場合はFAILとして現在のVMを保持し、
そのUUIDと独立watchdogを維持する。次回実行は稼働中VMを検出して停止する。
RAの表示が変わったことを別途検証し、NICコマンド成功をRAの切断・復帰成功として扱わない。

## Android

専用の`XM8M-E2E-21`、`XM8M-E2E-36`、`XM8M-E2E-37`と基準snapshotを準備する。
ARM64・API21/36/37.2を対象とし、compile/target SDKは変更しない。API21のRA分類は対象外。
`emulator` にSDKの実行ファイル、`activity` に実APKのActivity、`serial` に専用ポートを設定する。
基準snapshotをread-only/no-snapshot-saveで起動し、普段使いAVDは操作しない。
Android UI XMLを読み、SDLの描画はローカルのmacOS Vision OCRを併用する（swiftcが必要）。
タップはその直前の画面で一意に一致した文字の位置に限定する。不一致・曖昧な場合はFAIL。
Google Playのログインは不要。API19や実機での品質を確認済みとして扱わない。
37.2はmajorだけでなく`ro.build.version.sdk_minor=2`も確認する。
minor値の根拠は[AOSP Build.VERSION](https://android.googlesource.com/platform/frameworks/base/+/HEAD/core/java/android/os/Build.java)。

## VMPal Hub

```sh
python3 -m scripts.xm8_e2e.hub_scripts /absolute/artifacts/local-e2e/config.json
```

このコマンドは登録用の薄いホストスクリプト定義をJSONで出力するだけで、登録・実行しない。
登録する場合はVMPalの`vmpal_create_script`／`vmpal script create --host`を使用する。
VMPalの「Scripts on this Mac」の設定に従う。設定で拒否された場合は回避しない。
7環境×3分類を独立した入口として登録でき、どれも明示的なローカル入力を使用する。

## 自動層・受入記録との関係

`catalog.json`のreferenceは規範45・46・50との対応であって、そのカード全体の受入PASSではない。
既存`ra_acceptance.py`の人・Computer Use用台帳へ、無人スクリプトの結果を偽って転記しない。
本ランナーの観測結果は別runとして保存し、必要な人の確認・実績送信・内部計測の残件を保持する。

```sh
python3 -m unittest discover -s Tests -p local_e2e_test.py -v
```

これはランナーのテストであり、実際のXM8 GUI成功とは区別する。

## 校正・検証の進捗（2026-10-25）

現在の実行方式は既存VMの基準スナップショットへの復元である。以下のクローンでの結果は
方式変更前の履歴であり、既存VMでの再検証結果とは区別する。
既存Ubuntuでは初回RAログインと基準スナップショットの作成を確認した。
10月22日の分類実行はKeyring解除の準備で保留となり、6ケースとも未実施だった。
10月25日に同じ既存VMを起動し、手動解除後にRAオンライン6項目を校正した。
認証、ゲーム識別、Library、媒体操作、Casualの8→4→8MHz状態復元、Hardcoreの制限を確認した。
フォルダー選択・周波数読取りの失敗を保持し、調整して継続・再実行した結果であり、
基準snapshotからの分類一括実行と区別する。
記録は`artifacts/local-e2e/calibration/20261025-existing-ubuntu/report.md`。
基本分類と通信分類は、この既存VMでの分類一括再検証完了として扱わない。

Ubuntu 26.04.1 ARM64で、RA有効ビルドのRA mode OFFを使用し、基本操作7ケースを
VMPal Hubから通してPASSした。起動、マウスとキーによるメニュー操作、入力とフォーカス復帰、
設定変更後の再起動、state保存・変更・復元、両DriveのOpen・bank切替・Eject、正常終了と再起動を含む。
対象コミットは`5851050924baa01945a0875d87589a8d7036a871`、DEBのSHA-256は
`4ac1d9921f0cdffdc2ffaf5b287595c78489dad24d6ae7a91599605c78fe619f`。
ローカル記録は`artifacts/local-e2e/runs/20261020-225101-24c1d1b3/`に保存した。
同じ起動手順で存在しない画面文字列を要求した実行は、意図どおりFAILとなり、
一時クローンの削除まで確認した（`20261020-225527-7d98b2f7`）。

UbuntuのRAオンライン6ケースは、同じ隔離クローンで個別に再実行してPASS。
認証復元、媒体なしからのCasual設定・ディスク起動・Ys識別、Library詳細とSTART後のDrive構成、
両Driveのbank切替とDrive 2 Open/Eject、Casualのstate復元、HardcoreのFull Speed拒否・通常state項目制限・Debug Saveを観測した。
記録は`artifacts/local-e2e/calibration/20261021-080810/`。途中の判定失敗もattempt別に保持する。
続いて、新しいcold cloneからRAオンライン6ケースを分類一括実行し、6/6 PASSとクローン削除を確認した。
一括実行の記録は`artifacts/local-e2e/runs/20261021-083646-c55debb1/`。
実績解除・outboxの実サーバー受入、他OSは別途確認が必要。

`ubuntu-ra-recipes.example.json`は、RA ON・Hardcore選択済み・媒体なし・ROM起動の専用ベースから始める例。
現在の例はUbuntu 26.04.1 ARM64、1280×824、VMPal Tools 0.46、RA有効build 5851050の既存VMに合わせたもの。
以前の1280×754の記録とは判定領域を区別する。
テスト用コピー`E2E/E2E1.d88`・`E2E2.d88`には、RA ID 13682（Ys）のDISK_A/B/USER_DISKが必要。
自分の環境で校正するまでは例の`prepared:false`と`calibrated:false`を維持する。

`local_region_ocr:true`では、校正済み領域を拡大撮影してMac内のVisionで読む。
透過メニューの背景が文字判定を妨げる場合に使用し、画面寸法・fullscreen focusを照合する。
作業画像は一時ディレクトリから直ちに削除し、RAレポートには期待値の成立だけを残す。
VMPalのネイティブ操作とCLI入力を交互に使うと入力権が競合するため、実行中の操作はランナーに揃える。
LinuxゲストのNIC切断中も制御を継続できることと、独立した120秒の復帰タイマーは別途確認したが、
これはRAの通信復帰やoutbox再送のPASSではない。配布する設定・レシピ例は未校正のままとする。

Ubuntuの通信試験は、Casualの切断・復帰と、Hardcoreの切断中の制限維持・復帰が個別校正でPASS。
ケース間の正常終了・再起動をPIDと開始時刻で照合した。
切断確認後のランナー失敗・キャンセルを実ゲストへ注入し、オンライン表示への復帰とtimer停止も確認した。
記録は同校正runの`network/`と`restoration-injections.json`。
検証済みDrive 2／Drive 1／未検証媒体の違い、切断起動・再試行はPENDING。
`ubuntu-ra-network-recipes.example.json`に6ケースを示す。基準スナップショットからの分類実行は未校正のため準備完了にはしていない。
Linuxは正常復帰後に古いwatchdog timerを停止し、同じVMの次の切断へ干渉させない。
各切断で独立したguard名を発行する。復帰失敗時はtimerを残す。

10月25日の既存Ubuntuでは、切断・復帰、切断中のHardcore制限、同じsessionで検証済みのDrive 2交換がPASS。
Drive 1交換後、未検証Drive 2交換後、切断起動後の通常Resetによる復帰はFAILとして保持した。
Drive 1は元の既知Aへ戻しても、Reset後に`RA: pending`が続き、30秒以内にゲーム名へ戻らなかった。
アプリを正常終了・起動した後の認証復元・Ys識別は成功している。
その後の調査で、媒体交換後の2ケースは未送信実績の同期HTTPがタイムアウトし、識別前の正当な同期待ちで止まることを確認した。
診断用条件で接続再利用だけを無効にすると、同一プロセスで同期・識別が完了した。
切断起動では保存ログインが通信失敗のFailedとなり、通常Resetから再試行されない別の経路を確認した。
テスト側も認証・同期・識別の段階と未送信件数を区別して記録する必要があった。この調査時点では製品コードは未修正だった。
詳細は`artifacts/local-e2e/investigation/20261025/report.md`。診断条件での成功を配布版の合格へ置き換えない。
失敗・キャンセルを切断確認後に注入した実試験では、通信復帰と各watchdog timer停止がともにPASS。
記録は`artifacts/local-e2e/calibration/20261025-existing-ubuntu/network-report.md`。
未送信実績を発生させた試験ではなく、outbox再送の成功記録にはしない。


### 復帰処理の修正後（2026-10-25）

LinuxのRA APIは接続を再利用せず、ゲストNIC切断前の接続を次の要求へ引き継がないよう修正した。
新しい媒体起動／通常Resetでは、通信失敗した保存ログインを1回だけ再試行する。
以前のログインがまだ処理中なら、そのコールバックが完了してから再試行し、遅延応答との競合を避ける。
認証拒否は再利用せず、再試行失敗時に自動ループしない。エミュレーションコアは変更していない。

RA有効・Debugger無効・診断用差し替えなしのReleaseビルドで、既存Ubuntu ARM64の
Drive 1交換後、未検証Drive 2交換後、切断起動後の3ケースを再実行し、すべてPASSした。
各ケースの前にアプリを正常終了・再起動し、同じVMと解除済みKeyringを維持した。
これは指定3ケースの再検証であり、基準snapshotから通信6ケース全体を実行した結果ではない。
対象は5851050と上記修正、実行ファイルSHA-256は
`ffae0e0082b600707a4f539b5b308402924b119be2f436bf947af489ef6440d5`。
記録は`artifacts/local-e2e/recovery-verification/20261025-181146/report.md`と
`artifacts/local-e2e/builds/fix-ra-recovery-v2-20261025/manifest.json`。
macOS自動検証49件、Linux自動検証48件、ランナー自動テスト74件も成功した。

認証表示・識別表示・未送信／保留件数を段階別に保存し、認証復帰と識別復帰を区別した。
実際の未送信レコード1→0を観測したが、実績のサーバー側受入を独立に確認した結果とはしない。
途中でVMPal Toolsのデスクトップ側プロセスが停止した実行はPENDINGのまま保持した。
観測経路の復旧後に再実行し、例には切断時に次の操作を止めるガードを追加した。
他OS、実機、人による全実受入をこの結果でPASSへ変更しない。配布レシピ例の未校正設定も維持する。

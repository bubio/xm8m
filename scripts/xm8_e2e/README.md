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
ケースのFAILは、後始末がPENDINGやキャンセルになってもFAILのまま集計する。
操作中の例外と後始末の結果は別々に記録し、後始末自体の失敗も実行全体をFAILにする。
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
VMPal分類ではADBのdaemon初期化と一覧取得を分け、初期化時に出力パイプを継承させない。
一覧取得ができない場合も単台確認を省略せず、PENDINGで停止する。
ユーザーがE2E用と指定した既存VMをOSごとに1台だけ使う。
`vm_id`と`vm_name`を既存VMの正確なID・名前に固定し、`dedicated_vm:true`、
`baseline_snapshot`へそのVMの基準snapshot UUIDを指定する。名前の変更は不要。
分類別の上書きでVMや基準snapshotを変更することは禁止する。

開始時は停止中の同じVMをまず現在のディスクのまま起動し、`snapshot_ready`で現在の未送信データと
アプリ停止状態を読み取り検査する。初回実行や過去の`safe_to_restore:true`だけで検査を省略しない。
検査成功後にVMの正常停止を確認してから基準へ戻し、指定した配布物を導入する。
事前検査の成否と停止確認は`preflight.json`に記録する。検査不能・未送信データ・キャンセル・停止失敗では
復元不可の記録を維持し、現在のVM／ディスクを残してPENDINGにする。事前検査ではログイン情報を入力しない。
実行ごとのクローン、snapshot追加、VM削除は行わない。
ユーザーが作成したメモリー付き基準snapshotも使用できる。その場合は保存セッションを再開して
正常停止し、同じ基準ディスクをコールド起動して読み取り検査する。保存されたguest execの状態が
復元後に滞留しても、それを操作成功や検査成功として進めない。正常停止や検査に失敗したら保留する。
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

## macOS ARM64の基本操作

`macos-recipes.example.json`は未校正の基本操作例。既存の専用VMと基準snapshotを固定し、
RA有効の配布物をRA mode OFFで使う。BIOSは個人用bundle、媒体操作は
`Tests/generate_d88_fixtures.cpp`の合成D88をテスト用コピーとして配置する。
ROMや個人用bundle、基準画像をGitへ含めない。

`guests/macos.js`をゲストへ配置し、`macos_helper`と`macos_executable`を絶対パスで指定する。
JXA/AppKit/CoreGraphicsで実行ファイル・フォーカス・全画面寸法を確認するため、
ゲストにXcodeやアクセシビリティ権限を追加する必要はない。
macOSのF11と競合するため、`ensure_menu`はフォーカスされた全画面内への右クリックを使う。
OSのショートカット設定は変更しない。メニュー内のキー操作とマウス選択は別々に確認する。

`guests/macos.sh snapshot-ready`は、アプリ停止と回復用マーカーを確認し、DBの未送信件数を読む。
チェックポイント済みのWAL形式をAppleのsqlite3で読めるよう、停止中でWAL・SHM・rollback journalが
すべて存在しない場合だけimmutableの読み取りを使う。sidecarが残る場合やDBが壊れている場合は
巻き戻さず保留にする。schemaや未送信データを変更せず、件数0だけで再送成功とは扱わない。
DB本体が欠損していても、WAL・SHM・rollback journalが残っていれば保留にする。
回帰テストでは、未送信レコードをcommitした実際のSQLite WALを含め、両保存先で
DB欠損時のsidecarを検査し、ファイル不変とDBを新規作成しないことも確認する。
メモリー付きのmacOS基準は保存済みユーザーセッションを再開し、再開後にもDBを検査する。
巻き戻し前の現在データ検査は省略しない。起動時にログイン画面が出た場合は、実画面を確認して
ユーザーへ手動ログインを引き継ぎ、資格情報は自動入力しない。
`manual_sign_in_timeout`は30〜900秒。macOSの起動には`--no-sign-in`を指定し、VMPalの保存パスワードも入力しない。
待機時間はVMPal CLIの`--timeout`にも指定し、CLI既定の10分で先に終了しないようにする。
`resume_manual_sign_in:true`の場合、前回の巻き戻し前検査がログイン待ち等で保留になり、
まだ基準を復元していないことを記録で確認できる場合に限り、同じ起動中VMから再開できる。
現在データは再検査して正常停止した後で基準へ戻す。後片付け失敗や検査完了後の危険な記録を
この設定で解除したり、別VMを停止したりはしない。

2026-10-30、既存macOS Tahoe VM（26.6.2 / 25G83 ARM64）のClean install基準から、
`69488f9`のUniversal CI配布物を使い、RA OFF基本操作7ケースをVMPal Hubで一括PASSした。
最終runは`20261030-165413-f98a97b9`。入力・フォーカス復帰、設定保存、slot 9の保存・復元、
合成D88による両Drive操作、正常終了・再起動を画面で確認し、後片付けとVM正常停止まで通過した。
暖機状態での意図的な表示不一致は期待どおりFAILとなり、同じ後片付けも完了した。
ランナー回帰110件もPASS。記録は`artifacts/local-e2e/macos-preparation/summary.md`。
初期のF11競合、画像領域の混入、DB読取り・終了待ちの不具合、ログイン待ちの保留は記録を保持している。
クローン・追加snapshot・製品コード変更はない。macOSのRA分類と実機品質は未検証。

### macOSのRAオンライン

`macos-ra-recipes.example.json`はRAオンライン6ケースの未校正例。
同じmacOS専用VMに、RA OFF・Casual選択・媒体なし・アプリ正常終了の認証済み基準を用意し、
共通profileへVM・基準UUIDを設定し、`suites.ra`へ起動／検査／後片付けコマンドと校正済みレシピを設定する。
基本操作も同じ基準を参照し、配置するテスト用媒体だけを分類ごとに選ぶ。
元のClean install基準は保持し、VMのクローンは作らない。対象配布物と実行ファイルのハッシュを固定する。

初回RAログインとKeychainの保護された操作は、実画面を確認してユーザーへ引き継ぐ。
資格情報をホストへコピーしたり、自動入力したりしない。認証失効時は保留にする。
基準に含める設定・stateの有無とハッシュを保存し、試験後に同じ状態へ戻す。
RA DB・未送信データは保持し、現在ディスクと復元後の基準に対する検査を省略しない。

認証復元、ゲーム識別、Library詳細とSTART、両DriveのOpen／bank切替／Eject、
Casualのslot 9保存・復元、HardcoreのFull Speed拒否・通常state制限・Debug Saveを別ケースで観測する。
入力はSDLが検知できる長さで送信し、macOS用の画面領域で判定する。
RA画面の記録はアカウント情報を除く校正済み領域と、期待文字列の一致結果に限定する。
公開例の`prepared`と`calibrated`はfalseのまま配布する。

個別校正の成功は、基準からの6ケース一括成功とは区別する。
実績解除、outboxの実サーバー受入、通信切断・復帰、全媒体入口と実機品質はこの分類に含めない。

2026-10-31、既存macOS Tahoe VM（26.6.2 / 25G83 ARM64）の認証済み基準から、
`69488f9`のRA有効Universal配布物で6ケースを一括PASSした。
最終runは`20261031-090151-35d0bd1f`、VMPal Hub終了コード0。
設定・stateの復元ハッシュ照合、未送信データ検査、VM正常停止まで通過した。
ランナー111件とRA有効の全49テストもPASS。記録は`artifacts/local-e2e/macos-ra-preparation/summary.md`。
分類間の基準UUID不一致とOSログイン待ちの保留は保持し、最終一括PASSへ流用していない。

## Windows ARM64の基本操作

`windows-recipes.example.json` はWindows 11のRA OFF用の未校正レシピ例。
`screen_size` は保存PNGとポインターの座標系、`guest_screen_size` はWin32の全画面寸法であり、
VMPalが縮小表示すると両者は異なる。画面サイズ・固定領域・画像・パスは自分のVMで校正する。
`local_full_ocr` と `local_pointer_ocr` はMac内のOCRを使う。クリックは直前の画像で
期待文字列が一意に見つかる場合だけ行い、寸法違い・不一致・曖昧な候補では進めない。
キーやクリックの前には、指定したXM8実行ファイルのウィンドウがフォーカスされていることを検査する。
メインメニューはRA OFFヘッダーの固定画像も照合し、数値ラベルのOCR誤読の影響を避ける。
選択表示が点滅する項目には、`click_image`で固定領域と通常・反転の基準画像を照合できる。
基準は1〜3枚に限定し、一致した領域の中央だけをクリックする。クリック後は遷移先の
ヘッダー等を別の`expect_image`で確認し、操作前の表示だけで成功と判定しない。
媒体名の画像領域には、値が変動するFPS表示の端を含めない。

`guests/windows.ps1` はWin32で全画面・フォーカス遷移を観測し、`winsqlite3.dll` でoutboxを
読み取り専用で検査する。現在形式でoutbox表がない場合や不明なDBは復元を止める。
永続outbox導入前のschema 1/2は、outbox表が存在せず、schema番号を確認できた場合だけ許可する。
未送信・保留レコードや回復用マーカーは削除しない。helperはゲストへ配置する。
基準にhelperが含まれない場合、`snapshot_ready` は同スクリプトを自己完結したargvとして実行できるようにする。

アプリは対話ユーザーの専用Scheduled Taskから起動し、guest execの終了と寿命を分離する。
このタスクには資格情報を保存しない。`windows_helper` と `windows_executable` は実際の絶対パスに固定する。
SDL2のWindows保存先はユーザーのRoaming AppDataなので、`APPDATA`環境変数を変えただけで
隔離できたと見なさない。E2E専用VMの基準を使い、設定・stateの原本をゲスト内で保護し、
検証後に正常終了してから復元する。ROMと媒体原本は変更せず、媒体はテスト用コピーを使う。

`install_admin` は指定したゲスト内の前提ランタイム導入argvだけを管理者として実行する。
例えばVisual C++ ARM64ランタイムを用意する場合、Microsoft署名と固定SHA-256を検証する。
指定したXM8ビルドを変更したり、実行時に最新版へ差し替えたりする用途には使わない。
GUIログインや保護された許可画面の入力はユーザーが行う。

復元前検査のWindows実装は、XM8を停止したゲスト内で合成DBを使って検証できる。
これはGUIケースの成功ではない。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tests/windows_e2e_guest_test.ps1 -Helper C:\path\windows.ps1
```

2026-10-27、既存Windows11 ARM64 VMの25H2基準から、`bffdc72`のCI配布物を使い、
RA OFF基本操作7ケースをVMPal Hubから一括実行してPASSした（終了コード0）。
存在しない表示を要求する負の試験はFAIL・終了コード1となり、両実行で原本の復元とVMの正常停止を確認した。
クローンと追加snapshotは作成していない。ローカル記録は
`artifacts/local-e2e/windows-validation/summary.md`、最終runは`20261027-234741-722eda59`。
上記の基本操作記録はRA分類を含まない。公開例の未校正設定は維持する。

`windows-ra-recipes.example.json`は同じWindows VMのRAオンライン用レシピ例。
RA OFF・Casual選択・媒体なし・初回ログイン済みの停止状態を基準にする。
Windowsの資格情報はゲストのCredential Managerに保持し、ランナーへコピー・入力しない。
認証が復元されなければPENDINGとし、ログイン画面の表示を確認してユーザーへ引き継ぐ。
同じOSの基本操作とRA分類は、同一VM・同一基準snapshotへ揃える。
元の25H2基準を残し、準備済みの停止状態を別snapshotとして保存できる。クローンは作らない。

RA ONでは通常のLoad/Save項目が非表示になるため、System Optionsへの移動順も変わる。
透過メニューの背景がOCRを妨げる場合、`ensure_menu`にも校正した`region`を指定する。
見出しが読めないままF11で開閉した結果を成功扱いにしない。
Mac内のOCRが応答しない場合は、同じ一時画像の読取りだけを15秒上限・最大2回で試す。
キーやクリックは再送しない。観測できなければPENDINGで停止する。
Casualの保存・復元はテスト用slot 9、Hardcoreは許可済みのReset and Switchと
Full Speed拒否・通常state制限・手動Debug Saveを確認する。
使用媒体と両Driveの固定領域、ファイル一覧の選択順はWindowsで校正し直す。
認証行を含む画面を保存せず、`private_evidence`と`capture_region`を使う。

2026-10-28、同じ既存Windows11の認証済み停止基準から、`bffdc72`のARM64 CI配布物を使い、
RAオンライン6ケースをVMPal Hubで一括実行してPASSした（終了コード0）。
保存ログイン、Ys識別、Library、両Drive操作、Casualのstate復元、許可済みのHardcore切替と制限を確認した。
途中のファイル選択・OCR停止の失敗は保持し、キー選択と固定領域の観測、OCRの上限付き再試行を加えた。
ランナー自動テスト96件もPASS。最終runは`20261028-091824-09a13a25`、記録は
`artifacts/local-e2e/windows-ra-preparation/summary.md`。原本復元とVM正常停止まで確認した。
クローンは作らず、元の25H2を残して認証済み基準snapshotを追加した。
WindowsのRA通信切断・復帰は、2026-10-28に同じ認証済み基準から6ケース一括でPASSした。
最終runは`20261028-160934-eb3b099e`、記録は`artifacts/local-e2e/windows-network-preparation/summary.md`。
切断・復帰、Hardcore制限、検証済みDrive 2交換、Drive 1交換、未検証媒体、切断中の起動と再識別を確認した。
ケース間5回のアプリ再起動、6組のNIC状態照合と復帰タスク削除、原本復元・未送信データ検査・VM正常停止を確認した。
途中のOCR停止とbank選択失敗は記録を保持。Drive 2の非同期検証完了を装着表示で待ち、
メニューを開き直して選択する手順を、A/B交換10回と最終一括実行で検証した。
Python回帰102件、Windows復帰処理の模擬回帰6件、合成DB検査・集計22件もPASS。
実際の未送信実績によるoutbox再送と実サーバー受入は未検証。

`windows-ra-network-recipes.example.json`はWindowsの通信分類用の未校正例。
認証済みの同じ基準から開始し、Hardcoreを選び、各ケースの前に独立したオンライン状態を作る。
`restart_application_between_network_cases:true`では、VMを保持したままアプリを正常終了・再起動し、
PIDと開始時刻で新しいプロセスを確認する。元の設定・state 0を戻し、前ケースの媒体とセッションを持ち越さない。
この設定はWindowsの資格情報をエージェントへ取り出すものではない。

切断は専用ゲスト内の1枚のUpアダプターだけに行い、GUIDでDisabled／Upを照合する。
切断前にSYSTEMの120秒復帰タスクを登録し、電池動作・実行ポリシー・遅れた実行も考慮する。
試験の切断窓は90秒まで。正常復帰でタスクを解除し、復帰済みマーカーによって古いタスクが次の切断へ介入することを防ぐ。
Macの通信・firewall・hosts・VPNは変更しない。登録失敗では切断せず、復帰失敗では状態を残して停止する。

RA観測は認証行・状態行・短いゲーム名の領域を個別に校正する。
`ocr_cpu_english:true`はMacのVisionの計算デバイスをCPUへ明示し、英語UIだけを読む方式。
macOS 14以降が必要で、利用できなければPENDINGとする。既存の他分類のOCR方式は切り替えない。
Windowsの`observe-ra`は既定のSDL保存先にあるDBを読み取り専用で開き、未送信・保留の合計だけを返す。
空のDB・不明なschemaやstatusを成功件数0へ置き換えない。件数0だけでサーバーへの再送受入を成功と判断しない。
模擬アダプターによるWindowsの復帰回帰は`Tests/windows_network_guest_test.ps1`、
DB検査と集計の合成fixtureは`Tests/windows_e2e_guest_test.ps1`で確認できる。
どちらもExisting TestsのWindowsジョブで実行する。実NIC・実タスクやVM・RAサーバーは使用しない。

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

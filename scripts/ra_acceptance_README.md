# RA共通実受入ツール

Python 3.9以上・標準ライブラリのみ。4 OSで同じシナリオと証跡台帳を使う。
これは画面操作を自走するテストランナーではない。Computer UseまたはAndroid CLIを
使うエージェント／人が操作・観測し、このツールへ記録する。実行していない操作を
PASSにする機能、画像からreset回数を推測する機能は持たない。

仕様はDocuments/RetroAchievementsの45、運用は46、確認カードは50。
`ra_acceptance_cases.json`は50のオンライン1〜7・実回線断と46のStarting確認を
展開した初版。全AM自動試験、全ゲーム、全OS固有項目の完全な代替ではない。
Android SAF、タッチ、ライフサイクル等の追加受入は文書40も参照する。

## 新しいbuildで開始

macOSでは.appディレクトリではなく実行ファイル、Windowsではexe、Linuxでは
検証対象の実行ファイルまたはAppImage、Androidでは実際に導入するAPKを指定する。
revisionはビルド時のコード参照を記入する（ツールが証明する値ではない）。

```sh
python3 scripts/ra_acceptance.py init --run /tmp/xm8-macos-run --platform macos --build build-ra/xm8.app/Contents/MacOS/xm8 --revision e4bb9ae --device 'local Mac'
python3 scripts/ra_acceptance.py plan --run /tmp/xm8-macos-run
python3 scripts/ra_acceptance.py template --run /tmp/xm8-macos-run --case 50-01/Hardcore/dnd-library --output /tmp/ys-observation.json
```

runにはbuildのSHA-256、シナリオsnapshotとhash、仕様文書hash、OS、端末、UUIDを保存。
別build・別OS・別runの観測は受け付けない。並列書込みは未対応。同じrunを複数の
操作者で同時更新せず、端末ごとに別runにする。保存先はユーザー指定で、Gitへ入れない。
反復受入の記録はGit除外済みの`artifacts/ra-acceptance/日付-OS/`へ保存する。
以下の`/tmp`例は短時間の試行用。再起動等で消えることがあるため、継続記録には使わない。

## 操作層

- macOS: Computer Useで正確な.appを選ぶ。既存XM8プロセスとbuildを確認してから起動。
- Windows/Linux: 接続されたそのOSのComputer Useでウインドウ一覧から対象を選ぶ。
  macOSから自動的に遠隔操作できるとはしない。対象ホストの接続とbuildが必要。
- Android: `android-cli`スキルのinteract.mdを読み、`android info`／`android emulator list`
  で端末を確認。`android layout --help`／`android screen --help`を確認後、layoutを優先。
  SDL描画をlayoutで読めない場合はscreen captureし、画像を目視してから次の操作を決める。
  入力はスキルに従い`adb -s SERIAL shell input ...`。座標は直前の観測から取得する。
  device引数は各コマンドのhelpで確認し、対象を毎回明示する。端末未接続はNOT_RUN。
  SAFをD&Dと同じ操作として合格にしない。未提供入口は理由を記録し、OS固有項目を追加する。

座標をOS横断で使い回さない。操作後に最新の画面／layoutを確認する。
各ケースのsetupを満たし、操作順に証跡を残す。後の画面から前の状態を推定しない。
Computer Use出力にローカル画像がない場合、ツール呼出し参照と観測内容のテキストを
証跡にできるが、元画像を保存できたと記載してはならない。

## 観測と記録

テンプレートの`operator`はcomputer-use／android-cli／human。
`input_methods`にmouse／keyboard／touch／controller／cliを記録する。
`media`には原本パスや内容ではなく媒体aliasとbank、before/afterには両DriveとRA状態を記録。
`checks.*.actual`は実際に観測できたboolまたは整数。未観測はnullのまま。
reset回数を画面だけで断定できなければnull。将来の計測APIはまだ実装していない。

画面・layout・ログ・観測メモをrun/evidenceへ保存し、各checkのevidenceへ
`evidence/step-01.txt`等の相対パスを記入する。保存済み証跡は変更しない。
メモは日時・操作・ツール参照・観測事実・観測できなかった点を明記する。
この台帳は内容の真実性を自動証明しない。操作者の観測を型と期待値で照合する。

```sh
python3 scripts/ra_acceptance.py record --run /tmp/xm8-macos-run --input /tmp/ys-observation.json
python3 scripts/ra_acceptance.py report --run /tmp/xm8-macos-run
python3 scripts/ra_acceptance.py matrix /tmp/xm8-macos-run /tmp/xm8-android-run
```

- 全check一致・証跡あり: PASS。値不一致: FAIL。未観測あり: PENDING。
- 未実施はstate=NOT_RUN、reason必須。権限や端末待ちはPENDING＋reason。
- 自動試験をoperator=ciとして実受入へ登録することは拒否する。
- 再確認は新しいattemptとして追加。最新結果と過去の失敗を両方表示する。
- 台帳や画像が壊れた場合はエラーで停止。AIの高確信度で補完しない。
- matrixは複数runを並べる。OSやbuildを跨いでPASSを合成しない。

実回線切断はhuman限定。文書50の独立カードを先に渡し、XM8を開いたまま復旧し、
再送・サーバー反映を確認してから報告する。Keychainの入力・許可はユーザーに引き継ぐ。
ユーザーの設定・state・RA DB・媒体を削除・移動しない。

## TypeSafe（任意、補助判定のみ）

観測のうち外部送信可能な短い文章だけを別ファイルへ用意する。APIキー、ユーザー名、
原本パス、ROM/D88、認証画面、rawログを含めない。全文の機械的な匿名化は行わない。
最初はpayloadをローカルで確認する。`--send`を指定した場合だけTypeSafeへ送信する。

```sh
python3 scripts/ra_acceptance.py advice --run /tmp/xm8-macos-run --case 50-01/Hardcore/dnd-library --text-file /tmp/reviewed-observation.txt
python3 scripts/ra_acceptance.py advice --run /tmp/xm8-macos-run --case 50-01/Hardcore/dnd-library --text-file /tmp/reviewed-observation.txt --send
```

TYPESAFE_API_KEYを環境から読み、認証ヘッダーを保存しない。固定model=jev-1.13.0。
consistent/problem/not_run/insufficient、確信度・使用量・時間・入力をadviceへ保存する。
API失敗時も受入結果を変更しない。リダイレクトは拒否し、別ホストへキーを渡さない。
adviceはreportのPASS/FAIL計算から完全に除外する。閾値の妥当性は未評価。
公式: https://docs.typesafe.ai/api / https://docs.typesafe.ai/primitives/choice

## ツール自身の検証

```sh
python3 -m unittest discover -s Tests -p ra_acceptance_test.py
```

このテストは一時的な架空build・証跡を使い、ネットワーク・ユーザー媒体・GUIを使わない。
ツールのテスト成功をXM8実受入成功へ転記しない。

# CIテスト拡充の進行表

コア（Source/ePC-8801MA）は変更せず、内部処理をCIで確認できるよう段階的に拡充する。
各段階は変更、ローカル検証、ドラフトPR、対象コミットのCI結果を記録する。
VMのE2Eと実デバイス・実アカウントでの受入は別の検証層として区別する。

| 段階 | 対象と完了条件 | 状態 |
|---|---|---|
| 既存テストのCI実行 | macOS・Windows・LinuxのCTest、Androidの既存Javaテストを実行する | PR #89 merged |
| 設定・入力 | 保存・復元と破損設定、押下・解放・フォーカス喪失・割り当てを検証する | PR #90・#91 merged |
| 1 | 通常Appのセーブステート往復、I/O失敗通知・再試行を検証する | PR #92 merged |
| 2 | 音声バッファの折り返し・満杯・不足・再生停止を検証する | PR #93 merged |
| 3 | 描画先と座標変換、キーボード・マウス・タッチのメニュー選択・キャンセルを検証する | [PR #94](https://github.com/bubio/xm8m/pull/94) 実装済み・ドラフト |
| 4 | テスト用保存先での初回設定、終了保存と次回の反映、CLI設定の一時適用・復元、起動失敗を検証する | [PR #95](https://github.com/bubio/xm8m/pull/95) 実装済み・ドラフト |
| 5 | テープの再生・録音・取り出し・状態保存と復元、媒体消失・I/O失敗時のUI媒体状態を検証する | [PR #96](https://github.com/bubio/xm8m/pull/96) 実装済み・ドラフト |
| 6 | Windows x86・ARM64とAndroidの共通C++処理を実行するCIを追加し、OS・アーキテクチャ別の実行範囲を明記する | [PR #97](https://github.com/bubio/xm8m/pull/97)・[#98](https://github.com/bubio/xm8m/pull/98)・[#99](https://github.com/bubio/xm8m/pull/99) 実装済み・CI確認中 |

既存の通常Saveには、上書き失敗時の旧データ保持とLoad失敗時の全状態ロールバックの保証がない。
この保証を追加したと解釈しない。検証範囲の詳細は各テストのREADMEへ記録する。

ドラフトPRはmainへの取り込み前。各PRの最新CIを確認する。OS・CPU別の実行範囲とVM・実機に残す項目は、PR #99の `Tests/ci_coverage_matrix.md` にまとめる。

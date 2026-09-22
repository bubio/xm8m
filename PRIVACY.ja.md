# XM8M プライバシーポリシー

施行日: 2026-08-24

XM8Mは無料・非商用のオープンソースソフトウェアです。XM8M運営サーバー、telemetry、
analytics、広告、アプリ内課金、課金による機能差はありません。

## RetroAchievements

RetroAchievements（RA）は任意機能で、ユーザーがRA modeを有効にした場合だけRAへ直接
通信します。送信するのはRA APIに必要なアカウント名とログイン資格情報、ゲーム／媒体
hash、実績・LeaderboardのIDと結果、Hardcore状態、解除時刻／遅延、Rich Presence／
session情報、XM8M User-Agentです。ROM・D88の内容、ローカルファイルpath、ログイン後の
password、state内容は送信しません。RAサービスが受領した情報はRetroAchievementsが
独立して管理するため、同サービスのprivacy文書とアカウント機能を確認してください。
XM8Mはサーバーおよびデータウェアハウスを運営していないため、XM8Mについて開示すべき
サーバー所在地はありません。RetroAchievementsのインフラは同サービスが運営しており、
XM8M管理者はそのサーバー所在地を把握・管理せず、所在地について表明または保証しません。

## ローカルデータと保持期間

設定とRAデータは各OSのアプリデータ領域に保存します。RA用SQLiteにはlibrary metadata、
媒体hash、実績進捗、設定、画像cache metadata、保留解除を保存します。保留解除には
アカウント名、achievement ID、Hardcore flag、game hash、解除時刻、状態、再試行情報だけを
保存し、API tokenや生POSTは保存しません。同期成功、またはユーザーが確認したLogout／
RAデータ削除まで保持します。API tokenはLogoutまでOSの資格情報ストアに保持します。
badge画像は128 MiBのLRU cacheです。library、アプリ管理D88作業コピー、stateはユーザーが
削除するまで保持します。

Logoutは資格情報を削除します。保留解除がある場合は確認し、続行した場合は保留解除も
削除します。アプリデータ削除では、OSの動作に従ってRA database、cache、作業コピー、
state、資格情報が削除されます。AndroidではRA directoryと資格情報をcloud backupおよび
端末間転送から除外します。

## データ管理主体と問い合わせ

XM8M運営サーバーがないため、XM8M管理者はXM8M経由で個人データを受領・管理しません。
端末内データはユーザー自身が管理します。RAへ送信した個人データについては
RetroAchievementsがGDPR上のデータ管理主体であり、同サービスに対する開示・削除請求は
RetroAchievementsの窓口を利用してください。

英語版（規範）: https://github.com/bubio/xm8m/blob/main/PRIVACY.md

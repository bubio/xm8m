# XM8 Android Build

このディレクトリがAndroid版のビルド入口です。リポジトリルートではなく、
`Builder/Android` をAndroid Studioで開くか、このディレクトリのGradle Wrapperを
使用してください。

## 対応範囲

- 最低動作環境: Android 4.4 / API 19（RA利用時はAndroid 6.0 / API 23以上）
- compile SDK: Android 16 / API 36
- target SDK: API 36
- ABI: `armeabi-v7a`, `arm64-v8a`, `x86`, `x86_64`
- SDL: 2.32.10

Android 17 / API 37はSDKにインストールしていても、このプロジェクトでは採用しません。
API 37をtargetにすると大画面での画面固定・リサイズに追加の挙動変更が入るため、
compile SDKとtarget SDKはAPI 36に固定します。

## 必要な環境

- JDK 21（Android Studio同梱のJetBrains Runtimeを推奨）
- Android SDK Platform 36
- Android SDK Build-Tools 36.0.0以上
- Android NDK 23.2.8568313

GradleとAndroid Gradle PluginはWrapperから取得されます。

- Gradle 9.5.0
- Android Gradle Plugin 9.3.0

NDK r28以降は最低対応APIが21であるため、Android 4.4対応を維持するこのプロジェクト
では使用できません。NDKは必ず23.2.8568313をインストールしてください。

## SDLの準備

初回ビルド前、またはSDLを入れ直す場合に実行します。

```shell
cd Builder/Android
./setup_sdl2.sh
```

スクリプトは公式SDL 2.32.10を取得し、
`patches/sdl2-2.32.10-android-compat.patch` を適用してからnativeソースとAndroid
Javaソースを所定の場所へ配置します。

`app/src/main/java/org/libsdl/app` は生成先です。互換修正を追加・変更する場合は生成先
だけを直接編集せず、同じ変更を上記パッチへ反映してください。パッチにはAndroid
14以降のReceiver登録、Android 15のEdge-to-Edge、Bluetooth/録音権限、API 19互換
ガードが含まれます。

## コマンドラインビルド

Android Studioからの実行では同梱JBR 21が使われます。コマンドラインでは、同じJDK 21を
`JAVA_HOME` に設定してください。

```shell
cd Builder/Android
./gradlew assembleDebug
./gradlew assembleRelease
./gradlew lintDebug
```

RetroAchievementsは既定で同梱されます。単一APKはAndroid 4.4 / API 19以上で起動し、
Android 6.0 / API 23未満ではRAのUI・通信・資格情報保存を無効化します。RAなし版を作る
場合は、同じコマンドへ`-PXM8_ENABLE_RETROACHIEVEMENTS=0`を付けてください。

debug APKは以下へ生成されます。

```text
app/build/outputs/apk/debug/app-debug.apk
```

署名用の `release.keystore` がこのディレクトリに存在する場合、以下の環境変数を使用
してrelease APKへ署名します。

```text
KEYSTORE_PASSWORD
KEY_ALIAS
KEY_PASSWORD
```

## AARビルド

```shell
cd Builder/Android
./gradlew assembleDebug -PBUILD_AS_LIBRARY
```

以下の2つが生成されます。

```text
app/build/outputs/aar/app-debug.aar
app/build/outputs/aar/net.retropc.pi.aar
```

## 互換性上の注意

- Gradle/AGPはJDK 21で実行しますが、Javaコンパイル互換レベルはJava 8のままです。
  JDK 21の`javac`はJava 8のsource/target指定を将来廃止予定として警告しますが、現在の
  最低API互換性を保つための意図した設定です。警告だけを消す目的でJava 17へ上げたり、
  抑制設定を追加したりしないでください。
- native側の最低APIはRA同梱・RAなし版ともに`android-19`です。RA同梱版は実行時に
  API 23以上を確認してからKeystoreを使うRAサービスを初期化します。
- C++ランタイムはstatic linkし、NDK r23の4KB alignment版
  `libc++_shared.so` をAPKへ同梱しません。
- 64bit nativeライブラリは16KBページサイズ対応のELF alignmentでリンクします。
- API 30以上の没入表示は `WindowInsetsController`、API 19-29は従来のsystem UI
  visibility flagsを使用します。
- 起動時に「すべてのファイルへのアクセス」の設定画面を開きません。既存の直接ファイル閲覧と、既に付与された許可を維持するため、`MANAGE_EXTERNAL_STORAGE` の宣言は残します。直接ファイル閲覧に必要な場合はAndroidの設定から任意で許可できます。SDカード用の既存のStorage Access Framework動作も維持します。
- スクリーンショットは共有の `Pictures/XM8/Screenshots` に保存します。API 29以上ではMediaStoreを使用し、ストレージ権限は不要です。API 23〜28では既存のファイル閲覧用に起動時に書き込み権限を要求し、未許可なら保存操作時にも要求します。保存操作で許可した場合は、もう一度保存してください。

## 成果物の確認

16KBページサイズ対応はBuild-Toolsの `zipalign` とNDKの `llvm-readelf` で確認します。

```shell
zipalign -c -P 16 -v 4 app/build/outputs/apk/debug/app-debug.apk
```

4 ABIの各 `.so` について、LOAD segment alignmentが `0x4000` 以上であることも
確認してください。

実行確認では、少なくとも以下を確認します。

- API 19相当での起動
- API 35/36でのEdge-to-Edgeと没入表示
- Display Cutout
- ジェスチャーナビゲーションと3ボタンナビゲーション
- ROM読込、Intent起動、SAF
- 音声、タッチ、戻る操作、物理キーボード、USB HID

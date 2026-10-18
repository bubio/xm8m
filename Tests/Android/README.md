# Android共通C++テスト

既存の `fileio_error_test`、`setting_persistence_test`、`input_test`、`audio_test` をNDK 23.2.8568313でビルドし、Android 29 x86_64エミュレーター上で実行する。ビルドだけの確認と区別するため、各実行の終了コードと出力をJUnit XML artifactに記録する。

テストと製品の共通ソースはデスクトップ版と同じで、`__ANDROID__` の条件も有効。NDKのSDL2は製品と同じ準備スクリプト・パッチを使用する。コアや製品ソースは変更しない。

`host.cpp` はSDLActivityを起動しないテストのためSDLのmain準備を行い、JNIのストレージ判定で「専用の内部ファイル」と返す。FILEIO本体は変更せず、実際のAndroid用分岐とSDLによる読み書きを通す。SAFプロバイダーへの呼び出しが起きた場合はテスト失敗にする。

これらは既存CTestと同じ共通処理の単体テストで、RAアプリの別構成をビルド・起動するものではない。RA有効の製品APKは既存Androidワークフローで引き続き4 ABIをビルドし、Java単体テストも実行する。

## 手動で再実行する場合

```sh
<NDK>/ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Tests/Android/Android.mk \
 NDK_APPLICATION_MK=Tests/Android/Application.mk \
 NDK_OUT=<出力先>/obj NDK_LIBS_OUT=<出力先>/libs -j3
python3 scripts/run_android_native_tests.py <出力先>/libs/x86_64 --serial <対象シリアル>
```

実行スクリプトは対象デバイスを必須指定とし、`/data/local/tmp` 内に固有のテストディレクトリを作り、その中だけで生成ファイルを読み書き・削除する。既存アプリのデータや設定を操作しない。

## 対象外

ARM64・ARMv7・x86の実行確認、Androidバージョン網羅、SAF、Activityライフサイクル、画面回転・タッチ・実音声機器、GUI/実機での受入は別途必要。音声は既存audio_test同様にデバイス境界を置換し、バッファ・コールバック処理を検証する。

参考: [Android.mkのBUILD_EXECUTABLE](https://developer.android.com/ndk/guides/android_mk#build_executable)、[Android Emulator Runner](https://github.com/ReactiveCircus/android-emulator-runner)。

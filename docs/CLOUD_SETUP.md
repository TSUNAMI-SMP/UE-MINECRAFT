# クラウドで再実行できる検証

この環境ではUE Editor・Windows・GPUを利用できません。以下はMOD、書き出し／取り込み形式、UEから独立した計算と起動スクリプトの検証です。UEモジュールのコンパイルや実描画を確認した結果ではありません。

Java 21とGradle Wrapperは `python tools/setup_cloud.py` で準備できます。`tools/build_mod.py` は環境のプロキシとCAを使用し、証明書の検証を保持してビルドします。Javaテストと配布JARの作成：

```bash
python tools/build_mod.py test build
python -m unittest discover -s bridge/tests -p 'test_*.py'
python -m unittest discover -s tools -p 'test_native_*.py'
python tools/test_character_math.py
python tools/test_lighting_math.py
python tools/test_meshing_math.py
python tools/test_outline_math.py
python tools/test_mob_spawn_math.py
python tools/test_video_cadence.py
python tools/test_video_mask.py
python -m compileall -q tools bridge
git diff --check
```

PowerShellがPATHにある場合、Pythonのnative setupテストも実際のプラグイン設定関数を実行します。単独実行は `pwsh -NoProfile -File tools/test_native_build_plugins.ps1` です。既存のEngineAssociation・独自設定・プラグインmetadataの保持、有効化、再実行、重複拒否を確認します。PowerShellがない場合、この1件をスキップします。

今回の環境ではMicrosoftのDebian用PowerShell 7.4.19を公式リポジトリから取得し、公開されたSHA-256を照合して `/workspace/toolchains/powershell-7.4.19` へ展開しました。読み取り専用のホームへ書き込まないよう、XDGのcache・config・dataは `/workspace/toolchains/powershell-*` 配下へ設定しています。これはLinuxでの構文・設定関数の実行確認です。Windowsのエンジン検出・ビルド・ファイル選択・ゲーム起動は [NATIVE_PLAY.md](NATIVE_PLAY.md) の実機確認が必要です。

配布は検証済みJARと意図したソースをステージまたはコミットしてから、`python tools/package_bundle.py` と `python tools/package_ue_update.py` を実行します。既存の同名配布物は置き換えず、ZIPのCRCとSHA-256を確認します。

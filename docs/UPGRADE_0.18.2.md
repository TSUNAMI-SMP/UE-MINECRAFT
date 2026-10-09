# UEBridge 0.18.2：UI取り込みのPython読み込み修正

0.18.1のビルド後、UI取り込みで `ModuleNotFoundError: No module named import_minecraft_textures` が発生し、エラー処理によってUEが終了する問題を修正しました。UIから使用する発光・リアリスティックマテリアルの補助ファイルを絶対パスから直接読み込み、UEのPython検索パスに依存しなくしました。

0.18.1のC++修正も含む累積UE専用更新です。Minecraft MODは0.18.0を継続使用します。Minecraftからの再書き出しは不要です。

## 導入

1. UEを終了し、現在のUEBridgeフォルダーをバックアップします。
2. `UEBridge-update-0.18.2.zip` の中身を現在の `UEBridge` フォルダーへすべて上書きします。Sourceと15個のPython補助ファイルもすべて更新してください。
3. そのUEBridgeフォルダーでPowerShellを開き、以下を実行します。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Play-Native.ps1" -Reimport
```

ファイル選択画面で、0.18.0 MODで書き出した `native_manifest.json` を選びます。0.18.1のビルドが成功している場合、今回のPython修正にC++の再ビルドは不要です。0.18.0から直接更新する場合や、まだC++ビルドが成功していない場合は `-Rebuild` も追加してください。

ContentとSavedは削除しないでください。読み込み失敗時には新しいプレイ用マップは公開されず、従来のマップを保持します。

## 検証

Pythonの隔離モード（プロジェクトフォルダーを検索パスに追加せず実行）で両補助関数の読み込みを確認。UI検証と読み込み回帰テスト29件、リアリスティックマテリアル生成のモックテスト1件に成功。ZIPのCRCと全配布ソースの一致を確認。ユーザーのログでは0.18.1 C++ビルド成功を確認しました。

この環境にはWindows UEがなく、実際のテクスチャ・マテリアル取り込みの完走は未検証です。別のエラーが出た場合は新しいNativeImportログを添付してください。

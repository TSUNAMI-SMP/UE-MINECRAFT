# UEBridge 0.18.3：リアリスティックマテリアルのUV接続修正

UI取り込み中の `Realistic shader connection failed: Coordinates` を修正しました。テクスチャサンプルの入力にUEで使用される `UVs` を指定し、砂・TNT・岩・水・溶岩の接続を直しています。未完成の自動生成マテリアルは式を消してから再構築します。ユーザーのマテリアルやマップは削除しません。

0.18.1のC++修正と0.18.2のPython読み込み修正を含む累積UE専用更新です。MODは0.18.0を継続使用してください。Minecraftからの再書き出しは不要です。

## 導入

1. UEを終了し、UEBridgeフォルダーをバックアップします。
2. `UEBridge-update-0.18.3.zip` の中身を既存の `UEBridge` フォルダーへすべて上書きします。
3. 同じUEBridgeフォルダーのPowerShellで以下を実行します。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Play-Native.ps1" -Reimport
```

選択画面で現在使用している0.18.0の `native_manifest.json` を選んでください。0.18.1以降のC++ビルドが成功していれば再ビルドは不要です。0.18.0から直接更新する場合は `-Rebuild` も追加してください。Content・Savedの削除は不要です。

## 検証

モックの入力名検証を強化し、テクスチャでは `UVs` のみ、Custom式では定義済み入力のみ受け付けるようにしました。旧 `Coordinates` 指定が拒否されること、全14マテリアルの生成・保存・再利用が通ることを確認しています。マテリアル生成テスト1件、UI検証・隔離読み込みテスト29件が成功。配布ZIPのCRC・全ソースと補助ファイルの一致も検証しました。

Windows UE上での実際のマテリアルコンパイルとインポート完走はこの環境では未検証です。再度失敗した場合は新しいNativeImportログを添付してください。

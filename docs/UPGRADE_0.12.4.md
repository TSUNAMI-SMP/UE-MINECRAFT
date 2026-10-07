# UE照明マテリアル接続修正版0.12.4

[UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.4/downloads/UEBridge-update-0.12.4.zip)

0.12.3でWorld設定の停止を通過し、テクスチャ取り込みの照明生成まで進んだことを利用者ログで確認しました。今回の停止は `Native setup FAILED stage=textures: Cannot wire generated lighting: B` です。VertexColorの存在しない `RGBA` 出力名を照明ノードへ接続しようとしていました。

0.12.4ではモデルとアトラスの照明接続で、出力名を空文字にして先頭のRGB出力を使用します。R/G/Bの照明・AO情報を維持し、アトラスの頂点Aによる色付けとテクスチャAによる透明度の別接続も保持します。照明ON/OFF処理は継続します。失敗メッセージには接続元・接続先のノード型と出力・入力名を表示します。**実機での修正後の取り込み完了・描画は未確認です。**

## 0.12.3適用済みの場合

1. UE EditorとUEゲームを終了します。
2. 更新ZIPを一時フォルダーに展開します。
3. 使用中のUEBridgeフォルダー内の次の旧2ファイルを別の場所へバックアップし、ZIP直下の同名2ファイルだけをコピーして上書きします。

   ```text
   bridge_lighting_materials.py
   import_minecraft_atlas.py
   ```

   コピー先は次の `UEBridge.uproject` があるフォルダーです。

   ```text
   C:\Users\tsush\Downloads\UE-Minecraft-MVP-0.2.0\UE-MINECRAFT\unreal\UEBridge
   ```

4. WindowsのPowerShellで次の2行を順に実行します。

   ```powershell
   Set-Location -LiteralPath 'C:\Users\tsush\Downloads\UE-Minecraft-MVP-0.2.0\UE-MINECRAFT\unreal\UEBridge'
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Play-Native.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8' -Reimport
   ```

5. 前回と同じ `native_manifest.json` を選びます。自動取り込み中のUE画面は閉じずに待ちます。失敗時はPowerShellの出力全文か、表示された専用NativeImportログを保存してください。

今回のためのC++再ビルド、MOD交換、Minecraft再書き出しは不要です。Content／Config／Saved／既存レベルは削除しません。0.12.3未適用ならUEBridgeフォルダー全体をバックアップ後、ZIPのSourceと全 `.cmd`・`.ps1`・`.py` をuprojectの隣へ統合コピーしてください。ZIPにContent／Config／Saved／uproject／DLLは含みません。旧配布物とブランチは保持します。

## 照合と検証

Epic公式の [VertexColor出力](https://dev.epicgames.com/documentation/en-us/unreal-engine/constant-material-expressions-in-unreal-engine#vertexcolor) と [MaterialEditingLibrary Python API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/MaterialEditingLibrary?application_version=5.7) で、VertexColorのRGB/R/G/B/A出力と、空文字による先頭出力選択を確認しました。ドキュメントの照合はUE 5.8.3実機実行ではありません。

従来のテストは無効なVertexColor出力名も受け入れていたため、この問題を検出できませんでした。テスト用接続処理を厳格化し、旧コードで今回のB入力接続エラーが再現すること、修正後にモデル・不透明アトラス・半透明アトラスの照明とA出力が接続できることを確認しました。bridge88件とnative関連37件、計125件のPython検証が成功しました。照明計算、PowerShellの設定保持、Python構文、差分検査も成功しました。クラウドにはWindows／UE Editor／GPUがなく、実機取り込み・描画の確認は引き続き必要です。

```bash
python tools/package_ue_update.py --ue-version 0.12.4
```

# UE 5.8.3 ビルド修正 0.15.1

0.15.0の初回ビルドで発生した3種類のC++エラーを修正します。MODは0.15.0のまま使用します。再書き出しは不要です。

- モブ衝突判定：存在しないOverlapBlockingTestByObjectTypeをOverlapAnyTestByObjectTypeへ変更。
- インベントリ検索欄：SEditableTextBoxに存在しないBorderBackgroundColor設定を除去。
- HUD描画：GetPawnのTObjectPtrをAPawn*として受け取り、auto*推論エラーを解消。

## 適用

1. Unreal Editorを終了します。
2. 修正ZIPを展開します。
3. ZIPのSourceフォルダーを、使用中のUEBridgeフォルダーへコピーして3ファイルを上書きします。UEBridge/Source/UEBridge/*.cppとなる位置です。
4. 以下をPowerShellで実行します。別の場所へ展開している場合は最初のパスだけ変更してください。

```powershell
Set-Location -LiteralPath 'C:\Users\tsush\Documents\UE-Minecraft-MVP-0.15.0\UE-MINECRAFT\unreal\UEBridge'
$manifest = 'C:\UEBridgeTest\MC-Test\uebridge-export\native-2026-10-08T19-23-46.600781500-8a73c5f4\native_manifest.json'
.\Play-Native.ps1 -Manifest $manifest -Rebuild -Reimport
```

Content、Config、Saved、保存済みマップはこのZIPでは変更しません。
ビルド成功後に取り込みへ進みます。再び停止した場合は新しいビルド出力、Saved/Logs/NativeImport-*.log、Saved/Logs/UEBridge.logを確認します。

## 検証

既存のモブAI計算、モブ配置8チェック、描画計算1792ケース（5376色チャンネル）、空714チェックが成功。git diff --check、ZIP CRCと格納ファイルの一致を確認。
Windows UE 5.8.3での実コンパイル、入力、GPU描画、取り込み完了は未確認です。

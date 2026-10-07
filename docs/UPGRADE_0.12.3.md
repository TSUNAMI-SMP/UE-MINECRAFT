# UE取り込みAPI修正版0.12.3（MODは0.12.0を継続）

[UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.3/downloads/UEBridge-update-0.12.3.zip)

取り込み専用ログで、書き出しデータの検証完了後、`GameplayStatics.get_world_settings` が存在せず `AttributeError` で停止することを確認しました。0.12.3ではEditorのWorldに対して `get_world_settings()` を呼び、nativeレベルのGameModeを設定します。今回確認した停止原因への修正です。**実機での修正後の取り込み・描画成功は未確認です。**

## 0.12.2適用済みの場合

1. UE EditorとUEゲームを終了します。
2. 更新ZIPを一時フォルダーに展開します。
3. 使用中の `UEBridge` フォルダーにある旧 `import_native_play.py` を別の場所へバックアップし、ZIP直下の **`import_native_play.py` だけ** を次の場所へコピーして上書きします。

   ```text
   C:\Users\tsush\Downloads\UE-Minecraft-MVP-0.2.0\UE-MINECRAFT\unreal\UEBridge\import_native_play.py
   ```

4. WindowsのPowerShellを開き、次の2行を順に貼り付けて再取り込みします。

   ```powershell
   Set-Location -LiteralPath 'C:\Users\tsush\Downloads\UE-Minecraft-MVP-0.2.0\UE-MINECRAFT\unreal\UEBridge'
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Play-Native.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8' -Reimport
   ```

5. 前回と同じ完了済みパッケージの `native_manifest.json` を選びます。自動取り込み中のUE画面は閉じず、完了を待ちます。失敗時はPowerShellのエラー全文か、表示された `Saved\Logs\NativeImport-日時-識別子.log` を保存してください。

C++のソース、ランチャー、MODは0.12.2から変更していません。今回のためのC++再ビルド、MOD交換、Minecraft再書き出しは不要です。自動ランチャーがソース不一致を検出した場合は、通常どおり必要なビルドを行います。Content／Config／Saved／既存レベルを削除しません。

0.12.2未適用の場合は、UEBridgeフォルダー全体をバックアップ後、ZIPのSourceと全 `.cmd`・`.ps1`・`.py` を `UEBridge.uproject` の隣へ統合コピーします。ZIPにはContent／Config／Saved／uproject／生成DLLを含みません。[従来の配置手順](UPGRADE_0.12.1.md)も参照できます。

## 検証

Epic公式の [unreal.World Python API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/World?application_version=5.7) で `get_world_settings()` がWorld側のAPIであることを照合しました。公開ドキュメントの照合はUE 5.8.3実機での実行確認ではありません。

以前のテスト用UEモデルにも存在しないGameplayStaticsのメソッドを定義していたため、この問題を見逃していました。その定義を削除し、旧実装で今回と同じAttributeErrorが再現すること、修正後にnativeレベルのGameMode設定と完了記録作成が通過することを確認しました。既存 `/Game/UE` の設定が変更されないことも検証します。

native関連37件、bridge87件のPython計124件が成功しました。設定保持のPowerShell検証も実行済みです。Python構文と差分検査は成功しました。クラウドにはUE Editor／Windows／GPUがないため、取り込み後の地形、HUD、入力、描画の実機確認は引き続き必要です。旧配布ZIPと旧ブランチは保持します。

```bash
python tools/package_ue_update.py --ue-version 0.12.3
```

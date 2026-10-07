# 取り込み専用ログ更新0.12.2（MODは0.12.0を継続）

[UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.2/downloads/UEBridge-update-0.12.2.zip)

0.12.1のWindows／UE 5.8.3ビルドは、利用者のログで `Result: Succeeded` を確認しました。その後の取り込み失敗について、提示されたUEBridge.logは手動Editor起動と `/Game/UE` のbridgeモード再生を記録しており、取り込みのエラーが確認できません。**0.12.2は原因を取得するための診断更新です。取り込みの根本原因を修正したと断定していません。** UEでの取り込み・描画は未確認です。

## 適用と再試行

1. UE EditorとUEゲームを終了し、使用中のUEBridgeフォルダー全体を別の場所へコピーしてバックアップします。
2. ZIPを一時フォルダーに展開します。中の `Source` とすべての `.cmd`、`.ps1`、`.py` を、次のフォルダーへ統合コピーし、同名ファイルを上書きします。

   ```text
   C:\Users\tsush\Downloads\UE-Minecraft-MVP-0.2.0\UE-MINECRAFT\unreal\UEBridge
   ```

3. WindowsのPowerShellを開いて、次の2行を順に貼り付けます。PowerShell 7の追加インストールは不要です。

   ```powershell
   Set-Location -LiteralPath 'C:\Users\tsush\Downloads\UE-Minecraft-MVP-0.2.0\UE-MINECRAFT\unreal\UEBridge'
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Play-Native.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8' -Reimport
   ```

4. ファイル選択画面で、完了済み書き出しパッケージの `native_manifest.json` を選びます。自動取り込み中のUE画面は閉じず、完了を待ちます。
5. 失敗時は、ランチャーが表示したエラー全文をコピーします。長い場合は、表示された専用ログ `Saved\Logs\NativeImport-日時-識別子.log` の `Native automation failed`、`Traceback`、最後の例外行を含む範囲をコピーします。Pythonの起動前に停止した場合も専用ログの末尾を表示します。通常のUEBridge.logを開き直す必要はありません。

Content／Config／Saved／UEBridge.uprojectを削除しません。既存マップ `/Game/UE` とMinecraftのワールドは保持します。ZIPにはこれらの保護対象フォルダーや生成DLLを含みません。管理対象のnativeマップは成功時に更新します。MODは `minecraft-ue-bridge-0.12.0.jar` のままで、この診断更新のための再書き出しは不要です。旧配布物とブランチは保持します。

## 変更と検証

- 取り込みごとに日時と識別子を持つ専用ログを `-abslog` で指定します。後の通常Editor起動に上書きされません。
- Python例外の完全なTracebackをEditor終了要求前に記録します。
- 失敗時に終了コード、専用ログの絶対パス、エラー周辺と末尾80行を表示します。長い終了処理に埋もれたエラーも抽出します。
- 今回の取り込み識別子が完了記録に一致することを確認し、過去の完了記録による成功誤判定を防ぎます。既存の完了済みパッケージの通常再起動は継続できます。

Pythonのnative関連37件とbridge87件、計124件が成功しました。PowerShellではログなし・終了処理前の例外・Python起動前の停止の3ケースとランチャー構文、既存プラグイン設定保持の検証が成功しました。Python構文・差分検査も成功しました。クラウドでのPowerShell検証はLinux上の構文と診断関数の検証で、Windows上のUEプロセス起動・取り込み成功の確認ではありません。0.12.1のC++とMODは変更していません。

```bash
python tools/package_ue_update.py --ue-version 0.12.2
```

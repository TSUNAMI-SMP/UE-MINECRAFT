# UEビルド修正版0.12.1（MODは0.12.0を継続）

[UE更新ZIPをダウンロード](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.1/downloads/UEBridge-update-0.12.1.zip)

0.12.0のWindows 11／UE 5.8.3ビルドログを受けて、取り込み前に停止するコンパイルエラーへ対処したUE側の修正版です。**このクラウドにはUE Editor・Windows・GPUがなく、修正後のUEビルド・取り込み・描画成功は未確認です。** Minecraft完全移植の完成版ではありません。

MODは `minecraft-ue-bridge-0.12.0.jar` のままです。この修正のためのMOD交換やMinecraftでの再書き出しは不要です。完了済み0.12.0パッケージと `Saved/NativeWorlds` の保存形式を継続します。旧0.12.0配布物とブランチは保持し、修正版は `ue-native-play-0.12.1` で配布します。

## 適用する場所と操作

1. MinecraftとUE Editor、前回のUEゲームを終了します。使用中の `UEBridge` フォルダー全体を別の場所へコピーしてバックアップします。元の書き出しパッケージも保持します。
2. `UEBridge-update-0.12.1.zip` をダウンロードし、右クリック →「すべて展開」で一時フォルダーに展開します。
3. 展開したZIP内の `Source` を、次の **UEBridge.uprojectがあるフォルダー** へ統合コピーし、同名ファイルを上書きします。Source全体を削除する必要はありません。

   ```text
   C:\Users\tsush\Downloads\UE-Minecraft-MVP-0.2.0\UE-MINECRAFT\unreal\UEBridge
   ```

4. ZIP内のすべての `.cmd`、`.ps1`、`.py` ファイルも同じ `UEBridge` フォルダーへコピーし、同名ファイルを上書きします。説明用のMarkdownファイルは任意です。
5. コピー後、例えば次のファイルがこの位置にあることを確認します。`Source\Source` や `UEBridge\UEBridge` のような二重階層にしません。

   ```text
   UEBridge\UEBridge.uproject
   UEBridge\Source\UEBridge\BridgeNativePlayerController.cpp
   UEBridge\Source\UEBridge\BridgeReceiverNative.cpp
   UEBridge\Play-Native.cmd
   UEBridge\Play-Native.ps1
   UEBridge\Build-UEBridge.ps1
   UEBridge\import_native_play.py
   ```

6. **Play-Native.cmdをダブルクリック**します。ソースの変更を検出してビルドを再実行します。ファイル選択画面が出たら、以前書き出し完了したパッケージの `native_manifest.json` を選びます。エンジンの場所を聞かれた場合は `C:\Program Files\Epic Games\UE_5.8` を入力します。
7. ビルド結果が `Result: Succeeded` となった後、同じパッケージの取り込みとゲーム起動を待ちます。自動取り込み用に開いたUE画面は途中で閉じません。ビルドが失敗した場合は旧DLLで起動しません。

ZIPにはContent／Config／Saved／UEBridge.uprojectは含みません。これらを削除・置き換えず、フォルダー名の `0.2.0` もそのまま使います。既存レベルを保持し、取り込みは管理用 `/Game/Bridge/Native/NativePlay` へ行います。ビルド補助スクリプトは既存設定を保持して必要なプラグインを有効化し、uproject変更時はバックアップを作ります。

手動でビルドと取り込みを確実に再実行する場合は、WindowsのPowerShellを開き、次の2行を順に貼り付けます。PowerShell 7のインストールは不要です。

```powershell
Set-Location -LiteralPath 'C:\Users\tsush\Downloads\UE-Minecraft-MVP-0.2.0\UE-MINECRAFT\unreal\UEBridge'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Play-Native.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8' -Rebuild -Reimport
```

## 今回の修正

| ログの失敗 | 対処 |
| --- | --- |
| C3248：旧InputKeyはfinal | Controllerの宣言・定義を `FInputKeyEventArgs` 版へ移行し、必要なヘッダーを直接include |
| C2181：モブ復元ログのelse | UE_LOGをif／elseそれぞれの波括弧内へ移動 |
| C2039：Overlay slotのPadding_Lambda | 動的余白を属性対応のSBoxへ移し、検索欄の位置とサイズの追従を保持 |
| C2039／C2065：EKeys::PrintScreen | 存在しないキー定数への参照を除去。Print Screenへの割り当ては未対応として警告し、無効化 |
| C2664／C2665：JSONキーをTMapへ渡せない | 操作設定・カテゴリ音量のキーを明示的にFStringへコピー |
| C2665／C2100：GetNameSafe(PlayerAppearance) | 前方宣言だけだったUBridgePlayerAppearanceの型定義をinclude |
| C4458：Forceがクラスメンバーを隠す | 診断関数の引数をbForceLogへ変更 |
| C4305：背景色のdouble→float | floatリテラルを明示 |

`IncludeOrderVersion = Unreal5_6` のUpgrade案内は、今回の失敗原因ではありません。ビルド・取り込み・描画が確認できるまでは、インクルード順の変更を同時に加えません。

APIの照合にはEpic公式の [InputKey](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/APlayerController/InputKey)、[FInputKeyEventArgs](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FInputKeyEventArgs)、[JSONキー型](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Json/FJsonObjectSharedStringStorage)、[共有文字列](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/TSharedString)、[SBoxのPadding](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Slate/SBox/FArguments/Padding) を使用しました。ドキュメントとの照合はUEモジュールの実ビルドではありません。

## 検証と実機で確認する点

クラウドではPython123件を実行し、122件成功・PowerShellがないため設定保持の1件はスキップしました。UEから独立したC++の移動／入力67項目、光、地形、アウトライン、モブ召喚位置、映像タイミング・マスクの検証、Python構文と差分検査も成功しました。MODのソース・JARは変更していないため、今回はJavaの再ビルドを行っていません。ZIPは収録元ファイルとのバイト一致、CRC、SHA-256、保護対象パスが含まれないことを確認します。

実機ではビルド成功後、次を順に確認します。

1. 取り込みが完了し、地形とプレイヤーが同じ位置に現れる。
2. WASD、ジャンプ、視点、F5／割り当てたMouse4・Mouse5、F3、E、Escが反応する。
3. クリエイティブ検索欄をクリックして入力し、ウィンドウサイズ変更後も欄の位置が合う。日本語IMEの変換・確定は実機で確認する。
4. モブPaletteの名前・appearance／template数がログに出て、`missing_palette` で拒否されない。
5. 照明ON/OFF、爆発、FPSを確認し、保存→終了→Play-Native.cmdで同じパッケージの保存状態へ戻る。

途中で失敗した場合はそこで止め、ビルドなら最初のerrorから最後のResultまで、取り込みなら `Saved\Logs\UEBridge.log` の `Native setup FAILED`／`Native automation failed` と前のTracebackを保存します。成功していない工程を成功扱いにはしません。[対応範囲と残る制限](NATIVE_PLAY.md)も引き続き適用されます。

## 配布の再作成

意図したソースと手順をステージまたはコミットしてから、次を実行します。同名ZIPがある場合は上書きを拒否します。

```bash
python tools/package_ue_update.py --ue-version 0.12.1
```

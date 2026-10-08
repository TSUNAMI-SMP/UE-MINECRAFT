# 取り込み完了後の終了判定修正0.13.2

[UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.13.2/downloads/UEBridge-update-0.13.2.zip)

利用者の `NativeImport-20261008-152430-141056f2be864734aa071b29eb31dfc5.log` では、947ブロック種、1822ブロックモデル、1504アイテムモデル、74モブ外観・67テンプレート、プレイヤー、1504アイコン・464スプライト・590文字、924音イベント・2200音サンプルと描画設定を保存し、専用NativePlayマップを再読み込み・マップチェック（0エラー・0警告）しています。15:37:39に `Native setup COMPLETE` を記録し、15:37:46に終了ログを閉じました。

PowerShellが受け取った終了コード `-1073741819` はWindowsのアクセス違反（0xC0000005）です。このログにはアクセス違反のスタックがなく、UE内部の原因は特定できません。0.13.2はエンジンのクラッシュ自体を修正するものではなく、保存完了後の終了判定を修正します。

## 今回保存されたマップをすぐ起動

UEを終了した状態で、Windows PowerShellへ次のコマンドを貼り付けます。再取り込みせず、保存済み専用マップをUE単独ゲームとして開きます。

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' 'C:\Users\tsush\Downloads\UE-Minecraft-MVP-0.2.0\UE-MINECRAFT\unreal\UEBridge\UEBridge.uproject' '/Game/Bridge/Native/NativePlay' -game -windowed -ResX=1280 -ResY=720
```

Minecraftは終了したままで構いません。実際のゲーム起動・描画・操作はこの取り込みログだけでは確認できません。起動で停止した場合のゲーム側ログは `Saved\Logs\UEBridge.log` です。

## ランチャーの更新

1. UEを終了し、使用中の `Play-Native.ps1` を別の場所へコピーして保管します。
2. 更新ZIP直下の **Play-Native.ps1だけ** を、使用中の `UEBridge.uproject` がある次のフォルダーへコピーして上書きします。

   ```text
   C:\Users\tsush\Downloads\UE-Minecraft-MVP-0.2.0\UE-MINECRAFT\unreal\UEBridge
   ```

3. 以後はPlay-Native.cmdを起動します。旧ランチャーは今回の終了エラーで素材スクリプトの成功ハッシュを記録しなかったため、次回は再取り込みする可能性があります。すぐ試す場合は上の直接起動コマンドを使ってください。

0.13.1適用済みの場合、C++再ビルド・MOD交換・再書き出しは不要です。ZIPには以前のSource・Python修正も含まれます。Content、Config、Saved、uproject、ビルド済みDLLは含まず、旧配布物・マップ・UE保存・元ワールドを保持します。

## 判定と検証

- 通常の終了コード0でも、今回の試行ID・書き出しパス・SHA-256・完了フラグ・専用マップ名が一致し、非空のマップファイルが存在することを要求します。
- 非0で許容するのは今回観測したアクセス違反コードだけです。上記に加え、今回専用ログ内で対象書き出しのCOMPLETE、QUIT_EDITOR、Exiting、ログ閉鎖が順番に記録され、Python失敗・Fatal errorがないことを要求します。警告を表示して起動します。
- 古い試行／別の書き出し、欠落したマップ／ログ、途中停止、不明な終了コード、Python例外を成功扱いしないPowerShell回帰テストを実行しました。bridge 108件・native 38件、ZIPのCRC・収録ソース一致を検証します。
- 修正版ランチャーでのWindows実起動、ゲーム描画・操作、UE終了時アクセス違反の解消は未確認です。

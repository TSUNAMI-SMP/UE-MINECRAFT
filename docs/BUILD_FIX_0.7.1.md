# UE 0.7.0のC2668ビルド修正

[修正パッチZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-build-fix-0.7.1.zip)

BridgeVanillaEffects.cppでFMath::FRandRangeに整数を渡していたため、UE5.8のfloat版/double版の
どちらを選ぶか決められず、C2668でビルドが停止しました。3か所をfloatリテラルへ変更しています。
同じログのBridgeVideo.cppのC4996警告は、終了時のGPU待機APIを推奨のSubmitAndBlockUntilGPUIdleへ更新します。
IncludeOrderVersionの[Upgrade]表示は今回のビルド停止原因ではありません。

## 適用方法

1. UEとVisual Studioを閉じます。
2. 修正ZIPを別の場所へ展開します。
3. ZIPのSource/UEBridge内の **BridgeVanillaEffects.cppとBridgeVideo.cppの2ファイルだけ**を、次の場所へ上書きします。

```text
C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/Source/UEBridge/
```

Sourceフォルダー全体を削除せず、既存の2ファイルへ上書きしてください。

4. いつものUEBridge.uprojectの横にある **Build-UEBridge.cmd** を実行します。
5. 最後が **BUILD SUCCESSFUL** になったら同じUEBridge.uprojectを開き、保存済みレベルを読み込みます。
6. [0.7.0の導入手順](UPGRADE_0.7.0.md)の素材・スキン取り込みから続きを実行します。

MODは0.7.0のままです。Content/Config/Saved、レベル、既存の素材、プラグイン設定の変更は不要です。
UEのstatusバージョンも0.7.0のままになります。

## 確認範囲

実機ログにあるC2668の全3か所を修正し、リポジトリ内のFRandRange呼び出しも確認しました。
差分、配布ZIPの内容・CRC・SHA256を検証しています。
クラウドにはUE5.8/Windowsコンパイラがないため、修正後のUEビルドと描画はPCで確認してください。
元の0.7.0配布物を保持しているため、そのZIPを使う場合は本パッチを後から適用します。

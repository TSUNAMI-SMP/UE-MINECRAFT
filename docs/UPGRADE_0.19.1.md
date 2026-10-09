# UEBridge 0.19.1：UE 5.8のGetPawn戻り値に対応

0.19.0の水源処理で `auto* Pawn=PC->GetPawn()` としていた箇所を修正しました。利用者のUE 5.8.3では戻り値が `TObjectPtr<APawn>` であり、`auto*` の型推論に失敗してC3535／C2440でビルドが停止していました。`auto Pawn` で戻り値の型をそのまま受けるよう変更し、旧版の `APawn*` にも対応します。

## 適用

1. [UEBridge-update-0.19.1.zip](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.19.1.zip) を展開し、中身を既存の `unreal\UEBridge` へ上書きします。0.19.0の内容を含む累積更新です。ContentとSavedは保持します。
2. 同じフォルダーで以下を再実行します。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Play-Native.ps1" -Rebuild -Reimport
```

前回はビルドで停止しており、水アセットの取り込みまで進んでいないため、`-Reimport` も指定します。MODは0.18.0のまま、現在の `native_manifest.json` を使えます。起動ログは `Bridge UE 0.19.1 / MOD 0.18.0` になります。

変更を最小限で適用する場合は `Source/UEBridge/BridgeRealisticWorld.cpp` と `BridgeReceiverNative.cpp` の2ファイルを差し替えて同じコマンドを実行できます。

ログのC4686／C4701は警告で、今回の停止原因はGetPawnの型推論エラーです。水の制限とアセット準備は同梱の [NIAGARA_WATER.md](NIAGARA_WATER.md) を参照してください。修正後のWindows UEビルドと水の実描画は、このクラウド環境では未確認です。

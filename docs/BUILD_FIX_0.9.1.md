# UE 0.9.0のビルド修正パッチ0.9.1

[修正パッチZIPをダウンロード](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/UEBridge-build-fix-0.9.1.zip)

ユーザーのUE5.8ビルドログで確認された、次の4種類のエラーを修正します。

- `BridgeBlockGeometry.cpp` の先頭を、ファイル名に対応する `BridgeBlockGeometry.h` に変更。
- JSONの共有文字列キーを、長さ付きで明示的に `FString` へコピー。状態検索とvariantsのC2663/C2664を修正。
- `BridgeVideo.cpp` の存在しない `SetVolumetricCloud` を、雲の表示フラグ `SetCloud` に変更。
- `BridgeWorld.cpp` のローカル変数 `Role` を `ShapeRole` に変更。AActorのメンバー名との衝突C4458を修正。

`IncludeOrderVersion = Unreal5_6` の `[Upgrade]` メッセージは、今回のビルド停止原因ではありません。

## 適用手順

1. UE EditorとVisual Studioを終了します。
2. 上の修正ZIPをダウンロードし、別のフォルダーに展開します。
3. ZIPの `Source/UEBridge/` にある次の3ファイルを、使用中プロジェクトの同じ場所へ上書きします。

   ```text
   BridgeBlockGeometry.cpp
   BridgeVideo.cpp
   BridgeWorld.cpp
   ```

   上書き先は次です。

   ```text
   C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/Source/UEBridge/
   ```

   Source全体を削除せず、この3ファイルだけを上書きしてください。
4. 使用中プロジェクトの `UEBridge.uproject` と同じフォルダーにある `Build-UEBridge.cmd` を実行します。
5. 最後が成功になってから同じ `UEBridge.uproject` を開き、保存済みレベルを使います。
6. 素材の再書き出し・取り込みがまだなら、[0.9.0の導入手順](UPGRADE_0.9.0.md)の「2. 素材とモブを書き出す」から再開します。

**MODは0.9.0のままです。** このパッチだけのために素材を再取り込みする必要はありません。
Content/Config/Saved、保存済みレベル、取り込み済み素材、Python補助ファイルは変更しません。
UEのstatusに表示する版も0.9.0のままです。

## 確認範囲

添付ログに出た全4種類の原因と、同種のJSONキー受け渡し・雲フラグ・Role変数を確認しました。
ソース差分とZIPの収録内容・CRC・SHA256を検証しています。
クラウドにはUE5.8とWindowsコンパイラがないため、修正後のUEビルド・描画は未確認です。

元の0.9.0配布ZIPは保持しているため、それを使用する場合は本パッチを後から適用します。
`ue-bridge-0.9.0` ブランチのソースには、この修正を反映しています。

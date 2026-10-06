# UE 0.9.0のビルド修正パッチ0.9.2

[修正パッチZIPをダウンロード](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/UEBridge-build-fix-0.9.2.zip)

0.9.1の修正で使った `Pair.Key.GetData()` は、UE5.8のJSONキーの型
`UE::TSharedString<TCHAR>` に存在せず、C2039とC2665を起こしていました。
`BridgeBlockGeometry.cpp` の2か所を `const FString Key(*Pair.Key);` に修正します。

共有文字列型の公開ヘッダーでは、`operator*` がNULL終端された文字列ポインターを返します。
今回のビルドログにも `FString(const WIDECHAR*)` のコンストラクターが記載されています。
存在しないメンバー関数と、前回の長さ付きコンストラクター呼び出しを取り除きます。

このZIPは0.9.1のヘッダー順序・雲フラグ・Role変数の修正も含みます。
**0.9.0の更新ZIPへ、本パッチだけを後から適用できます。0.9.1を重ねる必要はありません。**
0.9.1を適用済みの場合も、本パッチを上書きしてください。

## 適用手順

1. UE EditorとVisual Studioを終了します。
2. 上の修正ZIPを別のフォルダーへ展開します。
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

   Source全体は削除しません。
4. 使用中の `UEBridge.uproject` と同じフォルダーの `Build-UEBridge.cmd` を実行します。
5. 成功を確認してから同じUEプロジェクトを開き、保存済みレベルを使います。
6. 0.9.0の素材更新がまだなら、[導入手順](UPGRADE_0.9.0.md)の「2. 素材とモブを書き出す」から続けます。

MODは0.9.0のままです。Content/Config/Saved、保存済みレベル、取り込み済み素材、
Python補助ファイルを保持します。このパッチだけを理由に素材を再取り込みする必要はありません。

## 確認範囲

添付ログのエラー2か所と、公開されているUE5.5の `Containers/SharedString.h` の
`TSharedString::operator*` 定義を確認しました。UE5.8のキー型は添付ログから確認しています。
ソース差分、ZIPの内容・CRC・SHA256、公開後のダウンロード内容を検証します。
クラウドにはUE5.8とWindowsコンパイラがないため、修正後のUEビルド・描画は未確認です。
`IncludeOrderVersion` の `[Upgrade]` 表示は、このログでのビルド停止原因ではありません。

旧0.9.0・0.9.1配布物は保持し、ブランチ `ue-bridge-0.9.0` のソースには本修正を反映します。

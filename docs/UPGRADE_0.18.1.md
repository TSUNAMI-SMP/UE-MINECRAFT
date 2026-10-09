# UEBridge 0.18.1：UE 5.8コンパイル修正

0.18.0のWindowsビルドログに記録されたコンパイルエラーを修正するUE専用更新です。Minecraft MODは0.18.0のまま使用してください。MODの入れ替えやMinecraftでの再書き出しは不要です。

## 導入

1. UEを終了し、現在のUEBridgeフォルダーをバックアップします。
2. `UEBridge-update-0.18.1.zip` を展開し、その中身を現在の `UEBridge` フォルダーへすべて上書きします。`Source` が既存の `Source` と同じ位置になるようにしてください。
3. UEBridgeフォルダーでPowerShellを開き、以下を実行します。ファイル選択画面が出たら0.18.0で書き出した `native_manifest.json` を選びます。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Play-Native.ps1" -Rebuild
```

すでにインポートが完了している場合は保存された出力先が再利用されます。別のエクスポートを選ぶ場合は `-Reimport` も追加してください。既存のContent・Savedは削除しないでください。

## 修正

- DLL公開クラス内のconstexpr定数を個別に宣言。
- 防具メッシュのTObjectPtrから明示的にポインターを取得し、継承メンバーとの名前衝突を解消。
- UE 5.8 JSON共有キーをFStringに明示変換（燃料・容器残留物・パーティクル）。
- TNTで無視するキャラクターの完全な型をinclude。
- 未定義DeltaSecondsを使う炉の更新をBeginPlayからTickへ移動。インベントリ・チャットを開いていても、一時停止中以外は炉を更新。
- リピーターの横方向ベクトルを成分ごとに反転。

## 検証と限界

レシピ計算・インベントリ個数保存・独立C++物理・60fps AVIの既存テストとZIP内容を確認。Windows UE 5.8.3の実コンパイルはこの環境で実行できていません。今回のログにあるエラーの原因を修正していますが、新たなコンパイルエラーが出た場合はその全文を送ってください。

ビルド失敗時は古いDLLを起動しない設計です。再実行時の最初の `error C` と末尾の結果が原因特定に必要です。

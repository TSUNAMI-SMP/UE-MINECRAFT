# UEBridge 0.19.3：Niagara水アセットのパラメータ名対応

UE 5.8.3の水アセットにある World Grid Extents と Num Cells Max Axis を接続コードが認識できず、水バケツの設置を拒否していました。空白入りと空白なしの両方のパラメータ名を受け付け、Vector3／Integerの型チェックは保持します。以前のWorldSpaceSize等にも対応します。未対応のアセットでは、実際のパラメータ名と型判定をログに出します。

## 適用

1. コンパイルに成功した NS_RealisticWater をUEエディターで保存し、UEを終了します。
2. [UEBridge-update-0.19.3.zip](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.19.3.zip) の中身を既存の unreal\UEBridge に上書きします。累積更新で、Content・Config・Savedは含みません。
3. UEBridgeフォルダーで次を実行します。既存の取り込み済み水アセットを使うため、今回は -Reimport は不要です。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Play-Native.ps1" -Rebuild
```

起動ログは Bridge UE 0.19.3 / MOD 0.18.0 です。平らな場所で水バケツを使用して確認してください。まだ拒否される場合はF3のActionと、UEBridge.logの Bridge Niagara water parameter: 行で原因を特定できます。領域外・水源数の制限は維持しています。

## 検証範囲

利用者の画面で、Generate Mesh Distance Fieldsを有効にした後のNiagaraアセットのコンパイル成功を確認しました。残る警告はエラーとは別です。この更新は接続名の互換修正で、砂・溶岩・TNT・爆発の見た目は変更していません。

この環境にはWindows UEがなく、今回のC++変更のUEビルド、水の実設置・衝突・描画品質・FPSは未確認です。実際のテンプレートの発生位置や水位などの調整は引き続き必要です。詳細は NIAGARA_WATER.md を参照してください。

# UEBridge 0.16.1 ビルド修正

**起動後の地形読み込みクラッシュは[累積修正0.16.2](UPGRADE_0.16.2.md)で修正しています。これから更新する場合は0.16.2のZIPを使用してください。**

0.16.0のWindows UE 5.8.3ビルドで報告されたコンパイルエラーを修正します。

- [UE修正ZIP 0.16.1](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.16.1.zip)
- [SHA-256](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.16.1.zip.sha256)
- [検証記録](AUDIT_0.16.1.md)

**MODは0.16.0のままです。0.16.0で完了したnative exportの再書き出しは不要です。**

## 適用と再実行

1. UEを終了し、修正ZIPを展開します。
2. ZIPの全内容を、実際に使用している `UEBridge.uproject` があるUEBridgeフォルダーへ上書きします。フォルダー名が0.15.0のままでも構いません。
3. 同じフォルダーでPowerShellを開き、次を実行します。最初の入力欄に、今回正常に書き出した `native_manifest.json` の実際の絶対パスを貼り付けます。

```powershell
$nativeManifestPath = (Read-Host "native_manifest.json の絶対パスを貼り付け").Trim().Trim('"')
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Play-Native.ps1" -Manifest $nativeManifestPath -Rebuild -Reimport
```

この修正は5つのC++ソース／ヘッダーの変更です。再ビルドが必要です。報告された試行はビルド前で止まり、取り込みが始まっていないため、上のコマンドには `-Reimport` も付けています。MOD交換、Minecraftの再起動・再書き出し、Content／Savedの削除は必要ありません。

## 原因と変更

| 原因 | 変更 |
| --- | --- |
| `Misc/LexFromString.h` が見つからない | 数値の解析を `Misc/DefaultValueHelper.h` の `FDefaultValueHelper::ParseDouble` に変更。有限値・非負整数・上限の検証を保持 |
| HUD処理のWidth／Heightがクラスメンバーを隠す（C4458） | ローカル変数をViewportWidth／ViewportHeightへ変更 |
| 流体の別cppからCellOfを参照できない（C3861） | ABridgeWorldの共通staticメンバーとして宣言・定義。負の座標でも床方向への除算を維持 |
| TArrayにCountByPredicateがない（C2039） | 変更前の行数を通常の読み取りループで数える。上限判定の後にデータを変更する順序を維持 |

ログのC2737（Before／Localを初期化する必要がある）は、上記の未解決API・関数から派生しています。今回の修正はそれらの原因を取り除きます。

Windows UEの再コンパイルは、このクラウドでは実行できません。修正後のコンパイル成功・取り込み・実描画は利用環境での再実行が必要です。再び停止した場合、最初のコンパイラーエラーから `Result:` までのログ、または `Saved/Logs/NativeImport-*.log` を保存してください。

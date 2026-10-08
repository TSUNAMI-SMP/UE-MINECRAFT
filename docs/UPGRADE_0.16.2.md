# UEBridge 0.16.2 起動時クラッシュ修正

0.16.0／0.16.1で、ゲーム起動後の地形読み込み中に発生した配列のAssertionを修正します。0.16.1のビルド修正も含みます。

- [UE修正ZIP 0.16.2](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.16.2.zip)
- [SHA-256](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.16.2.zip.sha256)
- [検証記録](AUDIT_0.16.2.md)

## 適用

1. UEを終了し、ZIPの全内容を、使用中の `UEBridge.uproject` があるUEBridgeフォルダーへ上書きします。
2. 同じフォルダーでPowerShellを開き、次を実行します。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Play-Native.ps1" -Rebuild
```

前回正常に取り込んだパッケージを使用します。MODは0.16.0のままです。今回の書き出しと生成済み素材は再利用でき、`-Reimport` は不要です。Content、Saved、元の書き出しを削除しないでください。

別のパッケージを指定する場合は次を使います。

```powershell
$nativeManifestPath = (Read-Host "native_manifest.json の絶対パスを貼り付け").Trim().Trim('"')
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Play-Native.ps1" -Manifest $nativeManifestPath -Rebuild
```

## 原因

クラッシュスタックは `ABridgeBlockPreview::Replace` の裏面追加処理を指しています。`TArray.Add` へ、変更中の同じ配列から取った要素の参照を直接渡していました。UEは自己参照の追加を拒否するため、次のAssertionで停止しました。

```text
Attempting to use a container element ... which already comes from the container being modified
ArrayNum: 6, SizeofElement: 4
```

修正では三角形の3つのインデックスをローカルの整数へコピーし、そのコピーを逆順に追加します。配列の内部メモリーが拡張されても参照が無効になりません。液体と専用アイテム形状の両面表示が対象です。裏面の追加自体は維持します。

変更した実行コードは `Source/UEBridge/BridgeBlockPreview.cpp` の1ファイルです。C++再ビルドが必要です。

独立した回帰検証では修正前の実コードで同じ自己参照の失敗を再現し、修正後の96ケースが成功しました。**Windows UEでの修正後の再ビルド・起動成功は、このクラウドでは確認していません。**

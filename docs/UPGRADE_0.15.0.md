# UE単独プレイ0.15.0 更新手順

0.15.0は、Minecraft Java 1.21.11で取得した実データをUEへ変換する取り込みを拡張した版です。照明・空・草ブロックのバイオーム色、水の占有、GUIのカテゴリ順、装備、攻撃インジケーター、攻撃音・無敵時間・ノックバック・死亡パーティクル、感度曲線、飛行慣性、モブの20Hz制御を更新しました。今回もWindows UE 5.8.3のC++コンパイル、シェーダー、GPU、IMEの実機検証はこの環境ではできません。

## 適用

1. MinecraftとUEを終了し、使用中のUEBridgeフォルダーと専用Minecraft環境をコピーします。
2. Minecraft 1.21.11 / Fabric / Java 21で、旧MODを0.15.0へ入れ替えます。
3. 反映したいワールドとリソースパックを開き、次を実行します。

   ```text
   /uebridge native export
   ```

   完了後の新しい `native_manifest.json` を控えてMinecraftを終了します。0.15.0では水、バイオーム色、天体情報、装備、GUI、攻撃属性、死亡パーティクル、ブロックの透明度・法線を追加取得するため、旧exportの再利用は推奨しません。

4. 更新ZIPの `Source`、`tools` のPythonヘルパー、`*.cmd`、`*.ps1` を使用中の `UEBridge` フォルダーへ統合コピーします。`Content`、`Config`、`Saved`、既存のマップは削除しません。
5. PowerShellで初回のビルド・取り込み・起動を行います。

   ```powershell
   Set-Location -LiteralPath 'C:\Users\tsush\Downloads\UE-Minecraft-MVP-0.2.0\UE-MINECRAFT\unreal\UEBridge'
   $manifest = (Read-Host '新しいnative_manifest.jsonの絶対パスを貼り付け').Trim('"')
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Play-Native.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8' -Manifest $manifest -Rebuild -Reimport
   ```

   画面が数分変化しない場合は `Saved/Logs/NativeImport-*.log` と `Saved/Logs/UEBridge.log` を確認します。`$manifest = ...` は変数へパスを代入するだけなので、そこでは取り込みは始まりません。上の `Play-Native.ps1` 行まで実行してください。

6. 初回取り込み後は同じ `Play-Native.cmd` で再開します。別のexportを選ぶと新しい保存IDになります。以前のUE保存を別IDへ自動移植する機能はありません。

## 0.15.0で確認する項目

- `status` の `deathPoofReady` と `particles.deathPoof` が `true` / `ready` になること。`missing_material` なら、取り込み後に同梱の `setup_vanilla_effects.py` をUE Pythonで実行し、BridgeReceiverへ粒子・死亡パーティクル材質を割り当てます。
- EでクリエイティブカテゴリがMinecraftの取得順で表示され、検索と「所持品へ」の切り替え、右クリック分割、Shift移動、削除枠、装備枠が動作すること。
- 攻撃インジケーター、満タン前の弱い攻撃、被ダメージの赤表示、10tickの無敵時間、ノックバック、死亡後20tickの消滅と白い死亡パーティクルを確認すること。
- 屋外・屋根下・夜、草ブロック、水、葉の重なりを確認すること。草ブロックの上面・側面・土面がそろわない場合は、古いworldではなく0.15.0で新しくexportしてください。
- 感度、スムーズカメラ、しゃがみ、三人称、クリエイティブ飛行の停止後の慣性を確認すること。

クラフト、全NBT/Data Components、全コンテナ、全モブ固有AI、雲・霧・End専用空、昼夜のUE内進行はまだ完全移植ではありません。MinecraftのJavaコードは参照してUEのデータ・計算へ変換していますが、Minecraftのクライアントコードやバニラ画像・音声を配布物へコピーしていません。

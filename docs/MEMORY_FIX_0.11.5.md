# UEメモリ修正パッチ0.11.5

[修正ZIPをダウンロード](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/UEBridge-memory-fix-0.11.5.zip)

`control ue`中に`BridgeBlockPreview::Replace`でページングファイル不足になった問題を修正します。セルの更新ごとにProceduralMeshComponentを作り直していたため、UEのレンダーリソース解放待ちが重なり、古いメッシュがメモリに残っていました。

## 適用手順

1. UEのPlayを停止し、Editorを終了します。
2. ZIPを展開します。
3. 次の2ファイルを、使用中のUEBridgeの同じ場所へ上書きします。

   ```text
   C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/Source/UEBridge/

   BridgeBlockPreview.cpp
   BridgeWorld.cpp
   ```

4. `Build-UEBridge.cmd`を実行し、`Result: Succeeded`を確認します。
5. UEを起動し、保存済みレベルを開いてからPlayします。

MOD交換、Python素材の再取り込み、Minecraft素材の再書き出しは不要です。Content／Config／Saved、保存済みレベルは削除しません。

## 変更内容

- 既存セルのProceduralMeshComponentを再利用し、メッシュセクションだけを更新します。
- 古いInstancedStaticMeshのインスタンスとProceduralMeshのセクションを破棄してから再構築します。
- セルの削除・再取り込み時にもコンポーネントの内容を先に解放します。

ログの`ページング ファイルが小さすぎる`はWindows側のメモリ不足を示します。修正後も大きな距離で同じエラーが出る場合は、UEとMinecraftを終了し、Windowsの仮想メモリを「すべてのドライブのページングファイルのサイズを自動的に管理」に戻して再起動してください。固定値を使う場合は空き容量を確認して十分なサイズを確保します。これはコード修正とは別の実機設定です。

## 検証範囲

Python65件、独立した地形・光・映像タイミング検証とソース差分チェックが成功しています。UE5.8でのメモリ使用量、映像30fps、実機描画はクラウドでは測定できません。

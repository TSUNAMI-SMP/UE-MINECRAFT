# 素材取り込み修正パッチ0.11.3

[修正ZIPをダウンロード](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/UEBridge-lighting-import-fix-0.11.3.zip)

0.11.2適用後に出た次の2つを修正します。

- 通常ブロックの素材で頂点光を使わない場合にもVertexColorを接続し、`Cannot wire generated lighting: B` で停止する問題。
- アトラス素材のClampノードに入力名がなく、`Missing Clamp input` でDefault Materialになる問題。

## 適用手順

1. UEのPlayを停止してレベルを保存し、UE Editorを終了します。
2. ZIPを展開し、次の3ファイルを、UEBridge.uprojectと同じフォルダーへ上書きします。

   ```text
   C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/

   bridge_lighting_materials.py
   import_minecraft_textures.py
   import_minecraft_atlas.py
   ```

3. UEを起動し、保存済みの `/Game/UE` を開きます。
4. Python入力で素材取り込みを最初から再実行します。

   ```python
   exec(open("C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/import_minecraft_player.py", encoding="utf-8").read())
   setup_minecraft_visuals("C:/UEBridgeTest/MC-Test")
   ```

前回の途中生成アセットは、取り込み処理が管理するBridge素材だけを再構築します。Content／Config／Saved、保存済みレベル、ユーザー素材は削除しません。MOD交換、C++再ビルド、Minecraft素材の再書き出しは不要です。

## BridgeReceiverのエラー

ログに `The current saved level must have exactly one BridgeReceiver` が出る場合は、Pythonを繰り返す前に `/Game/UE` を開き、Playを停止します。World OutlinerでBridgeReceiverを1個だけ残し、重複を削除してレベルを保存してください。Receiverを新しく追加して数を合わせるのではなく、既存の正しいReceiverを1個にします。

## 検証範囲

Python65件、光・地形・映像タイミングの独立検証が成功しています。UE5.8の実エディターでの素材取り込み、シェーダーコンパイル、描画結果はクラウドでは確認できません。

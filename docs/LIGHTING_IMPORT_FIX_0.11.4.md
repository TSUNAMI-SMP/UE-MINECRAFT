# 素材取り込み修正パッチ0.11.4

[修正ZIPをダウンロード](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/UEBridge-lighting-import-fix-0.11.4.zip)

0.11.3適用後にUE5.8で残った、`MaterialExpressionClamp`の入力接続問題を修正します。Clampを使わず、`Max(z, 0)`と`Max(-z, 0)`で上面・下面の係数を作るため、`Missing Clamp input`によるDefault Material化を避けます。0.11.3までのVertexColor、RGBA、CustomInput修正を含む累積版です。

## 適用手順

1. UEのPlayを停止してレベルを保存し、UE Editorを終了します。
2. ZIPを展開し、次の3ファイルをUEBridge.uprojectと同じフォルダーへ上書きします。

   ```text
   C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/

   bridge_lighting_materials.py
   import_minecraft_textures.py
   import_minecraft_atlas.py
   ```

3. UEを起動し、保存済みの`/Game/UE`を開きます。
4. Python入力で素材取り込みを最初から再実行します。

   ```python
   exec(open("C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/import_minecraft_player.py", encoding="utf-8").read())
   setup_minecraft_visuals("C:/UEBridgeTest/MC-Test")
   ```

MOD交換、C++再ビルド、Minecraft素材の再書き出しは不要です。Content／Config／Saved、保存済みレベル、ユーザー素材は削除しません。

`The current saved level must have exactly one BridgeReceiver` が出た場合は、`/Game/UE`を開いてPlayを停止し、World OutlinerのBridgeReceiverを1個だけにして保存してください。

## 検証範囲

Python65件、光・地形・映像タイミングの独立検証、パッチZIPの内容検証が成功しています。UE5.8実エディターでのシェーダーコンパイルと描画結果はクラウドでは未確認です。

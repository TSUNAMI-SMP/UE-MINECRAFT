# 素材取り込み修正パッチ0.11.2

[修正ZIPをダウンロード](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/UEBridge-lighting-import-fix-0.11.2.zip)

`setup_minecraft_visuals` の途中で `CustomInput(input_name=...)` に対し `TypeError: call() takes at most 0 arguments (1 given)` が発生する問題を修正します。UE5.8で引数なしのCustomInputを作り、そのinput_nameを編集プロパティとして設定します。腕等の面の陰影と光環境のカスタム式の両方を修正しています。

## 適用手順

1. UEのPlayを停止して、いつものレベルを保存します。
2. ZIPを展開し、`bridge_lighting_materials.py` **1ファイルだけ** を次へ上書きします。

   ```text
   C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/
   ```

   UEBridge.uprojectと同じ場所です。Sourceフォルダーには入れません。

3. UE出力ログをPython入力へ切り替え、次の2行をもう一度実行します。

   ```python
   exec(open("C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/import_minecraft_player.py", encoding="utf-8").read())
   setup_minecraft_visuals("C:/UEBridgeTest/MC-Test")
   ```

中断した取り込みを最初から再実行します。途中生成されたBridge用マテリアルも取り込み処理で再構築します。Content／Config／Saved、レベルや既存素材を削除しません。

**今回のパッチはMOD交換・C++再ビルド・素材再書き出し不要です。** MODは0.11.0、UEのコンパイル修正パッチ0.11.1は適用済みのまま使用します。0.11.0の素材を書き出していない場合だけ、初回導入手順の書き出しを先に行います。

添付ログはEditorの起動と保存済みレベルの読み込みまで進んでいます。冒頭のaqProf/Vtune/PIX等のDLL警告ではなく、末尾のPython例外が今回の素材取り込み停止箇所です。

## 検証

テスト用APIも実機ログと同じ引数なしCustomInputに制限し、生成された入力名と接続先を照合しました。Python65件が成功しています。クラウドにUE Editorはないため、実機でのPython実行・シェーダーコンパイル・描画成功は未確認です。

# 0.8.2：UE 5.8粒子素材のUV入力接続修正

[修正ZIPをダウンロード](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.8.0/downloads/UEBridge-particle-setup-fix-0.8.2.zip)

`Cannot connect dust material: MaterialExpressionAdd: -> MaterialExpressionTextureSampleParameter2D:Coordinates`
で停止する問題を修正しました。Texture Sampleのエディタ入力名はC++のフィールド名`Coordinates`とは異なります。
入力名を空文字にすると最初の入力を選ぶMaterialEditingLibraryの方法で、UV入力へ接続します。
0.8.1の`default_value`修正も含みます。

MOD/C++は0.8.0のままです。再ビルドやMinecraft素材の再書き出しは不要です。
以前のZIPは保持しています。0.8.0または0.8.1から、このパッチだけを適用してください。

## 適用方法

1. UEのPlayを停止し、レベルを保存します。
2. 修正ZIPを別の場所へ展開し、**setup_vanilla_effects.pyだけ**を次の同名ファイルへ上書きします。

   ```text
   C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/setup_vanilla_effects.py
   ```

   Source/Content/Config/Saved/uprojectは差し替えません。
3. 使用中の保存済みレベルが開いていて、BridgeReceiverが1個であることを確認します。
4. UEの出力ログの **Python入力欄** で、次を一行ずつ実行します。

   ```python
   exec(open("C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/setup_vanilla_effects.py", encoding="utf-8").read())
   setup_vanilla_effects()
   ```

   1行目も必ず再実行して、新しい関数定義を読み込みます。UEを再起動する必要はありません。
5. `Minecraft dust material assigned and saved` を確認します。
   途中まで作られた生成用素材は再構築され、Receiverへの割り当てとレベル保存まで実行します。
6. UE Play → `/uebridge import start` → READY → `/uebridge control ue` の順で起動し、石を破壊します。
   `/uebridge status` の素材と生成数に加え、画面に実際の粒子が見えることを確認します。

## 確認範囲

テストのエディタ代替を誤った入力名を拒否するように修正し、修正前の同じ接続失敗を再現しました。
修正後はPython27件成功。UV範囲・アルファ・再実行・失敗時の未割り当ても確認しています。
これらはPython補助のテストであり、UE 5.8シェーダーのコンパイル・GPU表示は実機確認が必要です。

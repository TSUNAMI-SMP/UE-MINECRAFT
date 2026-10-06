# 0.8.1：UE 5.8粒子素材設定スクリプト修正

[修正ZIPをダウンロード](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.8.0/downloads/UEBridge-particle-setup-fix-0.8.1.zip)

`MaterialExpressionPerInstanceCustomData: Failed to find property 'default_value'` で
素材設定が停止する問題を修正しました。UE 5.8で公開されていないプロパティの設定を削除しています。
粒子のUVオフセットはC++側から2チャンネルとも明示的に供給するため、この設定は不要です。

MODとC++ソースは0.8.0のままです。C++再ビルド、Minecraft素材の再書き出しは不要です。
元の0.8.0配布ZIPは保持しているため、使用する場合は本パッチも適用してください。

## 使用中プロジェクトへの適用

1. UEのPlayを停止してレベルを保存し、UEを閉じます。
2. 修正ZIPを別の場所に展開します。
3. 中の **setup_vanilla_effects.pyだけ** を次の場所の同名ファイルへ上書きします。

   ```text
   C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/setup_vanilla_effects.py
   ```

   Source/Content/Config/Saved/uproject、取り込み済み素材は上書きしません。
4. いつものUEBridge.uprojectを開き、使用中の保存済みレベルを読み込みます。
   Playは開始せず、BridgeReceiverが1個であることを確認します。
5. UEの出力ログの入力欄を **Python** に切り替え、次の2行を一行ずつ実行します。

   ```python
   exec(open("C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/setup_vanilla_effects.py", encoding="utf-8").read())
   setup_vanilla_effects()
   ```

6. `Minecraft dust material assigned and saved` が出ることを確認します。
   途中まで作られていた生成用マテリアルを再構築し、Receiverへ割り当ててレベルを保存します。
   Errorが出た場合は成功扱いにせず、その最初のエラーを確認します。
7. UE Play → `/uebridge import start` → READY → `/uebridge control ue` の順で再開します。
   石ブロックを破壊し、`/uebridge status` の `素材=true` と `生成` が増えるかを確認します。
   CPU登録数と素材の準備完了はGPU表示成功を保証しません。実際に粒子が見えることも確認します。

## 検証範囲

報告された非公開プロパティを拒否するテスト条件で、修正前の失敗を再現しました。
修正後はPythonテスト27件が成功し、ZIPのCRC・SHA256と同梱スクリプトを確認しています。
UE 5.8本体の実行・マテリアルのシェーダーコンパイル・描画はクラウドでは未検証です。

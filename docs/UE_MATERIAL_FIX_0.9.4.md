# UE腕・持ち物マテリアル修正0.9.4と初期転送の開始条件

[修正パッチZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/UEBridge-material-fix-0.9.4.zip)

## 初期地形がIDLE / 0/0のままの場合

Minecraftに「飛行を止め、地面に立ってしゃがまずに実行してください」が出ている場合、
`/uebridge import start` は開始条件を満たさず、地形を送っていません。
腕マテリアルの警告とは別の問題です。次の順に操作します。

1. UEで保存済みの `UE` レベルを開き、Playを開始して動かしたままにします。
2. Minecraftで `/uebridge control off` を実行します。MC操作へ戻り、UE映像の全画面表示を停止します。
3. 飛行中なら、Minecraftに設定しているジャンプキーを2回押して飛行を解除します。
   固体のブロックの上へ着地し、しゃがみキーを離します。しゃがみをトグル設定にしている場合は解除します。
4. 地面に立って静止した状態で、次を実行します。

   ```text
   /uebridge import start
   /uebridge import
   ```

5. `IDLE` から転送中へ変わり、`READY` になったら次を実行します。

   ```text
   /uebridge control ue
   ```

UEのPlayを途中で停止すると受信も止まります。停止した場合はPlayを開始し直します。
`ExplosionSystem is unset; Niagara will not play` は追加爆発エフェクトの未設定を示し、
この開始条件の拒否原因ではありません。

## 腕・持ち物の警告を修正する

`MaterialInstanceDynamic ... is not a valid parent ... Only Materials and MaterialInstanceConstants`
は、動的マテリアルを別の動的マテリアルの親にしていたコード不具合です。
合法な親マテリアルまで戻り、元の動的インスタンスのパラメーターをコピーしたうえで、
腕・持ち物用のインスタンスを生成するよう修正します。

1. UE EditorとVisual Studioを閉じます。
2. ZIPを別のフォルダーに展開します。
3. ZIP内の `Source/UEBridge/BridgeCharacter.cpp` **1ファイルだけ**を、次へ上書きします。

   ```text
   C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/Source/UEBridge/
   ```

4. 使用中の `UEBridge.uproject` と同じフォルダーの `Build-UEBridge.cmd` で再ビルドします。
5. 成功してから同じUEプロジェクトを開き、保存済みレベルを使います。

Source全体を削除しません。MODは0.9.3、Python補助ファイルは現在のものを保持します。
素材の再書き出し・再取り込みは、この修正だけのためには不要です。
Content/Config/Saved、レベル、素材を保持します。0.9.4はUEのこの1ファイルの修正であり、
0.9.2のビルド修正や0.9.3のMOD・取り込みスクリプトを置き換えるものではありません。

## 確認範囲

ユーザーのログ・画面と初期転送の条件を照合しました。
UE5.5の公開ヘッダー・実装で、動的マテリアルの `Parent` と `CopyInterpParameters` を確認し、
BridgeCharacterの3つの生成箇所を同じ安全な生成処理へ変更しました。
差分と配布ZIPの内容・CRC・SHA256を検証しています。
クラウドにはUE5.8がないため、修正後のUEビルド・警告解消・描画は実機確認が必要です。

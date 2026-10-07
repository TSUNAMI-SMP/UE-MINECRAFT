# 0.10.0：色・アウトライン・腕・持ち物・スポーンエッグ・飛行

MODとUEを両方更新し、素材を再書き出し・取り込みします。0.9.4までの修正を含みます。旧パッチの追加適用は不要です。
**クラウドでMODビルドとテストは成功しています。UE5.8のビルド・素材取り込み・描画・音・Windows統合動作は未確認です。**

## ダウンロード

- [MOD 0.10.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.10.0/downloads/minecraft-ue-bridge-0.10.0.jar)
- [使用中UEプロジェクト用の更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.10.0/downloads/UEBridge-update-0.10.0.zip)
- [ソース＋MOD一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.10.0/downloads/UE-Minecraft-MVP-0.10.0.zip)（新規導入用。使用中プロジェクトの置き換えには使いません）

以前のZIPとmainは保持しています。今回の配布ブランチは `ue-bridge-0.10.0` です。

## 1. 閉じて更新する

1. MinecraftとUEを終了します。使用中のUEBridgeフォルダーを別の場所へコピーしてバックアップします。
2. `C:/UEBridgeTest/MC-Test/mods/` の旧 `minecraft-ue-bridge-*.jar` を別フォルダーへ移し、新しい `minecraft-ue-bridge-0.10.0.jar` を入れます。本MODのJARは1個だけにします。Fabric APIなど他のMODはそのままです。
3. 更新ZIPを一時フォルダーへ展開します。中の `Source` を次の場所へ**統合コピー**し、同名ファイルを上書きします。Source全体は削除しません。

   ```text
   C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/
   ```

4. ZIP内の `Build-UEBridge.cmd`、`Build-UEBridge.ps1` と次の7個のPythonファイルも、同じUEBridgeフォルダーへ上書きコピーします。

   ```text
   setup_world_bridge.py
   import_minecraft_textures.py
   import_minecraft_player.py
   import_minecraft_items.py
   import_minecraft_mobs.py
   setup_vanilla_effects.py
   setup_bridge_rendering.py
   ```

更新ZIPにはContent/Config/Saved/uprojectは入っていません。これらを削除・置換せず、保存済みレベルと取り込み済み素材を継続使用します。

## 2. UEをビルドする

UEを閉じた状態で、使用中フォルダーの `Build-UEBridge.cmd` を実行します。
UEのインストールフォルダーを聞かれたら、実際のフォルダー、例えば `C:\Program Files\Epic Games\UE_5.8` を入力します。
ビルド成功を確認してから、いつもの `UEBridge.uproject` を開きます。エラーなら、そのビルドログを確認するまで次へ進みません。

いつもの保存済みレベル（現在の `/Game/UE`）を開き、Playを停止した状態で保存します。
World Outlinerに `BridgeReceiver` がちょうど1個必要です。新しいレベルやReceiverを重ねて作る必要はありません。

## 3. Minecraftで素材を書き出す

更新済みの専用Minecraftを起動し、専用シングルプレイのワールドに入ります。
UE操作中なら `/uebridge control minecraft` へ戻します。
使用するリソースパックを有効にし、表示したい剣などをホットバーへ入れます。
次のコマンドを1個ずつ実行し、それぞれ完了メッセージを待ちます。

```text
/uebridge textures export
/uebridge player export
/uebridge items export
/uebridge mobs export
```

`items export` は登録アイテムの標準モデルと現在のホットバー・オフハンドのモデルを、Minecraftの実際の描画処理から取得します。処理中はMinecraftが一時的に止まることがあります。
色・カスタムモデル等の構成が違う持ち物を追加した場合は、それをホットバーに入れて再書き出し・取り込みしてください。
現在のUE表示は主手のみです。

`mobs export` は近くのモブに加え、対応する地上モブのスポーンエッグ用標準素材も取得します。MCワールドへモブを自動追加しません。
今回のスポーンエッグを使うには**新版での再書き出しが必要**です。
複数回に分けてもよいですが、一括取り込みは最新のmanifestを使います。今回の標準テンプレートは各回に含まれます。個体ごとの外見はその回に近くにいた個体が対象です。

書き出し先は `C:/UEBridgeTest/MC-Test/uebridge-export/` です。画像・スキンはローカルに置き、GitHubへアップロードしません。音声はMinecraftが使用中リソースパックから直接再生します。

## 4. UEへ取り込む

UEのPlayを停止し、レベルを保存します。UEの出力ログを **Python入力** に切り替えて次を実行します。Windowsパスも `/` を使います。

```python
exec(open("C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/import_minecraft_player.py", encoding="utf-8").read())
setup_minecraft_visuals("C:/UEBridgeTest/MC-Test")
```

最新のブロック・プレイヤー・アイテム・モブ素材を取り込み、粒子を設定し、生成済みマテリアルのSpecularを0に補正します。照明に依存しない黒いアウトライン素材もReceiverへ割り当て、保存します。
既存ブロックのpaletteを維持してアイテムモデルを追加します。レベルは作り直しません。
エラーが出た場合は、最後の完了メッセージだけでなくTraceback全体を添付してください。

## 5. 起動と操作

1. UEでPlayを開始します。
2. MC側は飛行を止め、地面に立ち、しゃがまず `/uebridge import start` を実行します。
3. `/uebridge status` で初期地形が `READY` になったことを確認します。
4. `/uebridge control ue` を実行します。視点切り替えはMinecraftで設定しているキーを使います。

クリエイティブでは**設定済みジャンプキーを素早く2回**押すと飛行を切り替えます。飛行中はジャンプキーで上昇、しゃがみキーで下降し、着地で解除します。
サバイバルでは二度押ししても飛べません。サバイバルへ切り替えたら飛行を解除し、重力を戻します。モードはMinecraft側の実際のゲームモードで判定します。
これはUEの飛行許可の実装です。サバイバルの空腹・インベントリ消費・全ゲームルールを完成させる更新ではありません。

```text
/uebridge lighting on
/uebridge lighting off
```

OFFはMinecraftの実際の空を合成します。色の比較は同じ場所・同じリソースパックで行います。独自の露出補正を以前使っていた場合、比較前に `/uebridge video exposure 0` へ戻してください。
ONのUEエディタ表示とMC映像は、露出の履歴やViewportのAA設定まで完全には一致しません。

## 実機で確認する項目

- 同じブロックをON/OFFで比較し、白浮きが改善したか。黒いアウトラインが色変わりしないか。
- ドアを閉じた状態・開けた状態で、輪郭が薄い板のネイティブ選択形状に沿うか。階段等は複数の選択箱の外周になります。
- 静止・歩行・攻撃中の腕に二重像や急な跳ねがないか。腕の動作はUE側の振り時間へ統一し、ボブを補間し、映像のモーションブラーと時間AAを停止しました。
- 剣・食べ物・ブロックを順に持ち、各々のモデルとテクスチャになるか。
- 地上モブのスポーンエッグで、空きのある地面を右クリックしてUE側に出るか。出ない場合は操作理由と卵テンプレート数を確認します。
- 木のドア・トラップドア・ゲートの開閉音、レバーとボタンのON/OFF音。ドア2段で音が二重にならず、ボタンは石1秒／木1.5秒後の解除音が出るか。
- クリエイティブで飛行・上昇・下降・着地、サバイバルで飛行不可を確認します。

`/uebridge status` には持ち物モデル数、現在の主手モデル、飛行状態、映像色経路、モブの卵テンプレート数が追加されます。
`native item: minecraft:diamond_sword` 等はモデルを取得した状態です。`item model missing` は再書き出し・取り込みが必要です。簡易の棒には置き換えません。
UE取り込みログの `Native item models ready` に除外数が出ます。詳しい理由はローカルのitems書き出しの `manifest.json` 内の `excluded` にあります。

## 対応範囲と検証

通常のアイテムはネイティブの厚み・UV・色・一／三人称の持ち方を取得します。特殊な描画命令を使うアイテムは明示的に除外します。
エンチャントの光沢、リアルタイムの使用状態に応じたモデル更新、アニメーションテクスチャの連続再生、オフハンド表示は未対応です。全アイテムの全状態を実機確認したものではありません。
モブは対応する地上種の基本モデル・歩行・汎用AIです。飛行／水中種、種固有AI、装備等の追加描画層、ドロップは未完成です。1回の初期地形取り込みにつき累計128体までです。
看板・流体・特殊ブロックは前版の対象範囲を継続します。

映像はJPEG＋TCPを継続します。HDRの線形RGBを一度だけsRGBへ変換し、OFF時は同じ撮影からRGBと透明度を取得します。v4ではMC側がRGBを再度明るく補正しません。古いMODと新版UEを混ぜないでください。
OFFの追加マスク撮影は不要になりました。一方、HDR読み戻しの画素サイズは増えるので、実FPSや遅延の改善は実機計測が必要です。GPU共有テクスチャ方式は未実装です。

クラウドで実施：MODビルド、Java108件、Python49件、独立C++のキャラクター計算53項目・アウトライン8項目・マスク15往復。UE Automationテストはソースに追加していますが未実行です。

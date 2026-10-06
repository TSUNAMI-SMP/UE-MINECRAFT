# 更新0.9.0：モデル形状、腕・破片、地上モブ、照明と空

MODとUEソースを両方更新し、素材を再書き出し・再取り込みします。
0.8.2の粒子設定修正を含み、旧配布ZIPは保持します。
**0.9.0の配布ZIPには[UEビルド修正パッチ0.9.2](BUILD_FIX_0.9.2.md)も適用してください。** 元のZIPを保持しているため、後から修正パッチを上書きします。0.9.1の修正も含みます。

この版では一人称の固定FOV70投影、破片の調整、通常ブロックのモデルと当たり判定、
地上モブの表示・基本AI、照明オフと実際のMinecraftの空を実装しています。
**モブの全仕様は未完成です。** 飛行・水中移動、種類固有のAI・遠距離攻撃・クリーパー爆発、
繁殖・飼いならし・ドロップ、防具・羊毛・鞍などの追加描画は未対応です。
UE5.8のビルド・描画・Windowsでの統合動作はクラウドでは確認できません。

## ダウンロード

- [MOD 0.9.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/minecraft-ue-bridge-0.9.0.jar)
- [既存UEプロジェクト用更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/UEBridge-update-0.9.0.zip)
- [ソースとMODの一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/UE-Minecraft-MVP-0.9.0.zip)

使用中のプロジェクトには「既存UEプロジェクト用更新ZIP」を使ってください。
配布ブランチは `ue-bridge-0.9.0`。Minecraftの画像・音・個人スキンは配布物に含みません。

## 1. UEとMODを更新する

1. MinecraftとUE Editorを終了します。
2. 使用中のUEプロジェクトをバックアップします。現在の場所は次です。

   ```text
   C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/
   ```

3. 更新ZIPを別フォルダーに展開します。ZIPの中の `Source` を使用中プロジェクトの
   `Source` に**統合コピー**し、同名ファイルを上書きします。Source全体は削除しません。
4. 次のファイルも `UEBridge.uproject` と同じフォルダーへコピーします。

   ```text
   Build-UEBridge.cmd
   Build-UEBridge.ps1
   setup_world_bridge.py
   import_minecraft_textures.py
   import_minecraft_player.py
   import_minecraft_mobs.py
   setup_vanilla_effects.py
   ```

   `Content` / `Config` / `Saved` / 保存済みレベル / 取り込み済み素材を保持します。
   更新ZIPにはこれらや `.uproject` の置き換えファイルを含めていません。
5. [UEビルド修正パッチ0.9.2](BUILD_FIX_0.9.2.md)の3ファイルを先に上書きします。
   UEを閉じたまま、使用中フォルダーの `Build-UEBridge.cmd` をダブルクリックします。
   成功を確認してから、そのフォルダーの `UEBridge.uproject` を開きます。
6. `C:/UEBridgeTest/MC-Test/mods/` 内の旧 `minecraft-ue-bridge-*.jar` をゲームフォルダー外へ退避し、
   `minecraft-ue-bridge-0.9.0.jar` を1個だけ配置します。Fabric APIは保持します。
7. 専用Minecraft 1.21.11/Fabric環境を起動します。LAN公開していないシングルプレイ・
   クリエイティブのテストワールドを使います。既存サーバーへの導入は不要です。

## 2. 素材とモブを書き出す

UEのPlayを停止しておきます。MinecraftではまだUE操作に切り替えません。

1. テスト位置の周辺に、牛・豚・ゾンビなど確認したい地上モブを置きます。
   素材の書き出し対象は読み込まれている64ブロック以内のモブ、最大128体です。
   UEへ実際に取り込む対象は初期地形の範囲内です。
2. Minecraftのチャットで順に実行し、各完了メッセージを待ちます。

   ```text
   /uebridge textures export
   /uebridge player export
   /uebridge mobs export
   ```

3. `/uebridge textures` で書き出し結果、`/uebridge mobs` でモブ転送状態を確認できます。
   モブ書き出しの詳しい除外理由はローカルの `uebridge-export/mobs-*/manifest.json` の
   `skipped` に記録します。対象モブがない場合は書き出しできません。

全アイテムの専用モデル対応は今回の範囲に含めていません。
通常ブロックの持ち物は新しいモデル形状を使い、それ以外の持ち物は簡易表示です。

## 3. UEに取り込む

1. いつも使う保存済みレベルを開き、**Play停止・レベル保存済み・BridgeReceiverが1個**を確認します。
2. UEのOutput Logの入力モードをPythonにして、次の2行を1行ずつ実行します。

   ```python
   exec(open("C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/import_minecraft_player.py", encoding="utf-8").read())
   setup_minecraft_visuals("C:/UEBridgeTest/MC-Test")
   ```

   最新のブロックモデル・スキン・粒子を取り込み、最新のモブ書き出しがあればモブ素材も取り込みます。
   ファイルの日付フォルダーを手入力する必要はありません。Windowsパスは `/` を使います。
3. モブだけ追加取り込みする場合は次を実行します。

   ```python
   exec(open("C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/import_minecraft_mobs.py", encoding="utf-8").read())
   setup_minecraft_mobs("C:/UEBridgeTest/MC-Test")
   ```

4. 赤いPythonエラーがないことを確認し、レベルを保存します。
   素材はローカルで生成・再利用し、既存アセットを削除しません。
   0.8系のテクスチャパレットだけでは新しい地形を初期転送できないため、必ず再書き出し・取り込みします。

## 4. 起動と実機確認

```text
UE Play
→ /uebridge import start
→ /uebridge import で READY を確認
→ /uebridge control ue
```

初期転送時にMCワールドを変更せず、UEの設置・破壊は同じPlay中に保持します。
地形の転送範囲は既存設定のままです。モデル種類の対応が広がっても無限のワールド転送にはなりません。
モブはUE操作開始時に1回取り込み、UEの死亡後に同じモブを再生成しません。
元のMinecraftモブはUE操作中だけ更新を停止し、操作停止・通信途絶で復帰します。
元モブの位置・健康状態・NBTは書き換えません。UEの結果もMCワールドへ保存しません。

次の順で確認してください。

| 内容 | 実機で見ること |
|---|---|
| 腕 | 素手・ブロック・細い腕のスキン。世界FOV80とダッシュ中でも腕の基本投影はFOV70 |
| 破片 | 石の破壊、ハーフブロックの破壊、ダッシュ。通常歩行では粒子なし |
| 形状 | 階段・ハーフブロック・柵・板ガラス・植物・ボタンの形と向き |
| 衝突・照準 | 階段の段、柵の高さ、薄いブロックの狙いと破壊、非衝突の植物 |
| 設置 | 上下ハーフ、2枚のハーフ結合、階段の向き、隣接した柵・階段の形 |
| 操作 | 右クリックで木のドア・トラップドア・柵ゲート、レバー・ボタン。しゃがみ中は設置優先 |
| モブ | 歩行・徘徊、敵の追跡・近接攻撃、プレイヤーからの攻撃・死亡、音 |
| モブ停止 | `/uebridge control off` 後にMC元モブが通常動作へ戻ること |
| 空 | 以下の照明コマンドで空・太陽・月・雲が表示され、物体と破片の境界が自然であること |

視点切り替えはMinecraftの設定済みキーを使います。サイドボタンの割り当てを保持し、F5固定にはしません。
腕は世界と同じ深度描画を使うため、至近距離の地形による遮蔽はバニラと違う場合があります。

## 5. 照明と空、破片の調整

```text
/uebridge lighting off
```

UEからMinecraftへ送る映像の照明をオフにし、全画面でMinecraftの実際の空と合成します。
空・太陽・月・星・雲は、同じ映像フレームのUEカメラ位置・角度に合わせて描画し、MCの時刻・天候を使います。
UE Editorの作業用Viewportの表示モードはそのままです。
`/uebridge lighting on` で通常のUE映像へ戻します。
`/uebridge sky ue` は照明オフのままUE背景を表示する診断用です。
`/uebridge video pip` は小窓表示へ戻し、Minecraftの空との合成を停止します。

UE背景が残る場合は、Play停止後、UEの空用ActorのDetails → Actor → Tagsに
`UEBridgeSky` を追加し、レベルを保存してからPlayをやり直します。
スカイマテリアルの自動除外に加え、このタグのActorも映像から除外します。

雨・雪の降水形状は合成しません。天候による空と雲の変化を反映します。
JPEG＋TCPを継続し、空モードは追加撮影とマスク転送があるため、FPS向上・低遅延化を保証しません。
GPU共有テクスチャ方式は未実装です。

破片の初期値はサイズ0.75・密度1・寿命0.9です。次は例です。

```text
/uebridge particles scale 0.6
/uebridge particles density 0.75
/uebridge particles lifetime 0.8
```

サイズと寿命は0.25〜2、密度は0.125〜1。設定を保存し、映像有効時にUEへ送ります。
通常立方体の標準64個は維持し、非フルブロックは輪郭形状に応じて生成します。
粒子素材が設定済みでも、0.9.0へのソース更新・再ビルドは必要です。

敵の攻撃はUEの体力に反映し、Minecraftの保存済み健康状態は変更しません。
全画面HUDにUE体力を表示します。体力0でUE移動と編集を停止し、
`/uebridge respawn` でUE初期位置へ復帰します。復帰位置に障害物がある場合は操作結果に理由を表示します。

## 対象と除外

通常のバニラモデルを持つブロックについて、状態・variants・multipart・回転・UV・色・透過素材と、
Minecraftから取得した衝突／輪郭形状を扱います。看板・流体・BlockEntityを持つブロック
（チェスト、かまど、ベッド等の専用処理）、専用描画、ポータル、ピストン、レッドストーン配線、
トリップワイヤー、不可視管理ブロックなどは今回の転送から除外します。
書き出しの `manifest.json` の `excluded` にIDと理由を記録します。

植生の成長、重力ブロックの落下、レッドストーン回路、作物・骨粉などの全ゲーム挙動は未実装です。
モデルに複数のランダム候補がある場合は先頭候補を使い、アニメーションテクスチャは最初のフレームを使います。
リソースパック固有のプログラム式モデル・全アイテムモデルは対象外です。

モブは初期スナップショットの地上モブが対象です。飛行・水中の20種類は転送対象外で、
モブの追加発生・種類固有のAI・追加描画層は今後の実装が必要です。
身体の衝突はUEのカプセルで近似します。

## 診断と検証

`/uebridge status` を実行し、問題の起きた直後の画面かチャット文字列を共有してください。
ブロックで止まる場合は `モデルエラー`、空で止まる場合は `MC空` と `マスク=物体/全画素`、
粒子では素材準備・生成・拒否・登録数、モブでは `/uebridge mobs` の素材不足／期限切れも確認します。
Pythonエラーでは最初の `Traceback` から最後のエラー行までを共有してください。
UEビルドエラーでは、最後の「失敗」だけでなく最初のC++エラーとファイル名・行番号が必要です。

クラウドでの検証結果は公開時のREADMEに記載します。C++計算テストやPythonの素材検証は、
UEモジュールのコンパイル・GPU描画・実機統合テストの代わりにはなりません。

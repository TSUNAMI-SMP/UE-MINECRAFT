# 0.5.0：Minecraftは入力とHUD、UEは移動と衝突

今回の実装は**UE主体の移動・衝突と、一度だけの初期地形転送**です。
腕・持ち物、UEへのアイテム使用、リアルな水、Chaos特殊破壊は次の段階です。
従来の位置コピー方式も残し、明示コマンドで新モードへ切り替えます。

## 更新

1. Minecraftを終了。UEで今のレベルを保存し、UEとVisual Studioを閉じる。
2. [MOD 0.5.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.5.0.jar)へ交換。
   modsに旧Bridge JARを残さない。Minecraft 1.21.11、Java21、Fabric/APIはそのまま。
3. [UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-update-0.5.0.zip)を別フォルダへ展開。
4. **Sourceフォルダを丸ごと**、Build-UEBridge.cmd/.ps1、setup_world_bridge.py、import_minecraft_textures.pyを、
   いつも開くUEBridge.uprojectと同じフォルダへ上書きコピー。Source/Sourceと二重にしない。
   パッチはContent/Config/Savedを含まない。今のレベル・736種の素材・パレットを継続使用できる。
5. Build-UEBridge.cmdを実行し、BUILD SUCCESSFULを確認。その同じuprojectを開き、保存済みレベルを読み込む。
6. GameModeはBridgeGameMode、PawnはBridgeCharacter、BridgeReceiverは1個。
   Texture Paletteは既存DA_MinecraftPalette。**素材の再インポートは不要**。
   0.4.3の取り込み修正も新しいスクリプトへ含めています。

## 最初に作るテスト地形

専用シングルプレイ・クリエイティブを使ってください。マルチプレイでは新モードを開始できません。
Minecraft側に床・高さ2〜3ブロックの壁・階段・1ブロックの台を作る。
UE側の元の床やLandscapeが重なると、そちらに衝突します。専用レベルで該当Actorを選び、
**Actor Hidden In GameをON、Actor Enable CollisionをOFF**にしてテストする。
LandscapeStreamingProxyが複数ある場合は対象範囲をまとめて確認する。
非表示だけでは当たり判定は消えません。Niagara/Chaosの壁の設定は保持する。

## 地形転送とUE操作

1. UEで通常のPlay（1 Player）を開始。
2. Minecraftのテストワールドに入り、**飛行を止め、地面に立ち、しゃがみを解除**する。
3. `/uebridge status`でUE=0.5.0、Camera=trueを確認。
4. 初回は範囲を小さくするため、次を順番に実行：

   ```text
   /uebridge world radius 1
   /uebridge import start
   ```

   import startは、UEの同期地形をMinecraftから作り直す明示操作です。
   再実行した場合、前回のUE側の地形変更を置換します。
5. `/uebridge import`で進捗確認。BEGIN→COPYING→COMMIT→**READY**まで待つ。
   初期転送中はMinecraftの移動・ジャンプ・通常の攻撃/使用を固定する。視点とHUDは使える。
   radius1は27領域、横24×縦24×奥行24ブロック。radius2は75領域、40×24×40。
   埋まったブロックも含めるため、数十秒〜数分かかる場合がある。
6. READYになったら実行：

   ```text
   /uebridge control ue
   /uebridge status
   ```

   UE映像が全画面になる。statusの**UE判定=true / 保持=true**を確認。
   WASD、Space、Shift、マウスで操作。移動・重力・床/壁の衝突・ジャンプ・しゃがみはUEが計算する。
   斜め移動は速度を正規化。ジャンプは押し始めに一度。天井に当たる場所では立ち上がりをUEが判定する。
7. 抜けるには `/uebridge control off`。UE入力を止め、映像をOFFにしてMinecraft操作へ戻る。
   UEの取り込んだ地形は保持し、Minecraftの地形変更で上書きしない。
   同じ接続・同じPlay中なら `/uebridge control ue`で再開できる。

## 当たり判定の確認

- Minecraftの壁に対応するUEの壁へ向かって歩き、貫通せず止まる。
- 台にジャンプして乗り、UEの重力で床へ着地する。
- 階段/半ブロックで移動を確認。MCのCollision Shapeの直方体群をUEの実形状として使うため、表示が以前のOutline Shapeと異なる場合がある。
- 低い天井でしゃがみ、天井下でShiftを離しても無理に立ち上がらないことを確認する。
- 取り込み範囲の端で止まる。今回は固定範囲で、範囲外は見えない境界で囲っている。
- Minecraft側のF3位置は固定され、UE位置がstatusへ返る。これは今回の意図した動作。
  MinecraftプレイヤーをUE位置へ移動させる処理は入れず、バニラの衝突やチャンク移動を操作結果に混ぜない。
- インベントリ/チャット中は移動キーが中立になる。通信が250ms途絶えるとUEの移動・重力を停止する。

## 初期転送後の保持とUE側の編集

READY後はMinecraftの定期走査を停止し、UEもworld_cell/world_scope/world_clearを受け取って地形を上書きしない。
旧`/uebridge world refresh`は新モードでは置換しない。明示`import start`だけが初期地形を作り直す。

BridgeReceiverにBlueprint Callable **Remove Imported Blocks**を追加した。
PositionはUEワールド座標(cm)、Radiusはcm。READY後に球内の形状中心に該当するものを除去し、表示と衝突を更新する。
これは今後の特殊破壊用の入口で、今回Minecraftのクリックへはまだ接続していない。
必要ならLevel BlueprintでBridgeReceiver参照→Remove Imported Blocks(Position,Radius)を呼び、
削除した場所が再走査やcontrol off/ueで復活しないことを確認できる。
個々のブロックは直方体群で、階段等は一部形状だけが削除される場合がある。破片・Niagaraは自動生成しない。

**保持は同じUE Play実行中のメモリ内です。Play停止・UE終了後のディスク保存は未実装。**
UE Playを再起動したらLOSTになり、自動で再転送しない。`control off`で戻し、地面に立って`import start`をやり直す。
Minecraftのワールド退出/再接続でも新たにimportを明示開始する。古い取り込みが残っている場合も自動で破壊しない。

## 範囲と未実装

- Minecraftは入力とHUDを提供。Minecraftのサーバー側プレイヤー・インベントリはまだ通常の状態であり、
  UEのHP/所持品とHUDを同期する処理は今後の実装。試作は専用クリエイティブ限定。
- UEモードの通常の攻撃・アイテム使用・ブロック破壊は抑制。水バケツ/TNT/弓のUE照準操作は未実装。
  既存のTNT/弓イベントは従来のMinecraft操作モード向け。UEモードでは発動しない。
- 腕、持ち物、F5三人称、スプリント、飛行、泳ぎ、流体物理、特殊なブロック効果、Mob、HPは未実装。
- 水・草花等は表示のみで非衝突。葉/ガラスの透過、全状態/UVの再現は以前の制限を継続。
- MCのCollision Shapeを初期静的形状へ変換するが、その後の判定・移動はUE側のみ。
- 地形上限は8192形状/領域、64色、256ID/色/衝突グループ、全体131072形状。
  大きすぎる範囲や複雑な地形で転送が進まない場合は範囲を小さくして再試行する。
- チャンク未ロード領域は空として確定せずロードを待つ。小さい範囲でも止まる場合はMC描画距離とstatusを確認。

## 検証

クラウド：MODのJava21ビルド、JUnit39件、Python10件成功。
初期転送の状態確認/再試行/UE再起動時の停止、衝突付きパケットのサイズ/指紋、UDP位置返信の検証・順序・期限を確認。
Minecraft1.21.11の対象クラスに新しいMixinの対象メソッドが存在することを確認。
UE Protocol/World Automationを追加したが**未実行**。クラウドにはUE Editor/Windowsビルドツールがない。
UE5.8のC++ビルド、MODのGUI起動、実際の衝突・カメラ・通信を組み合わせた動作確認はPCで必要。

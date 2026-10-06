# 0.7.0：自分のスキン、視点切り替え、音と粒子

Minecraft Java 1.21.11 / Fabric / Java 21、UE 5.8。両方を更新します。
現在使用中のUEBridgeと専用Minecraftテスト環境で作業してください。
フォルダー名がUE-Minecraft-MVP-0.2.0のままでも、更新したファイルが入っていれば問題ありません。

## 1. MODとUEソースを更新

1. Minecraftを終了。UEでレベルを保存してUEとVisual Studioを閉じます。
2. [MOD 0.7.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.7.0.jar)をダウンロード。
   専用ゲームフォルダーのmods内の古いBridge MODだけを外し、新しいJARを入れます。Fabric APIは残します。
3. [UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-update-0.7.0.zip)を展開。
   **中の全ファイルとSourceフォルダーを、使用中のUEBridge.uprojectのあるフォルダーへ統合コピー**します。
   同名ファイルを上書きします。Sourceを先に削除したり、Sourceの中へもう一つSourceを作ったりしないでください。
4. 次の並びを確認し、同じ場所のBuild-UEBridge.cmdをダブルクリック。

```text
UEBridge/
  UEBridge.uproject
  Build-UEBridge.cmd
  Build-UEBridge.ps1
  import_minecraft_textures.py
  import_minecraft_player.py
  setup_vanilla_effects.py
  Source/UEBridge/BridgeCharacter.cpp
  Source/UEBridge/BridgePlayerAppearance.h
  Source/UEBridge/BridgeVanillaEffects.cpp
  Content/              ← 既存のレベル・素材
```

必要ならUEの場所 `C:\Program Files\Epic Games\UE_5.8` を入力します。
最後が **BUILD SUCCESSFUL** になってから、このUEBridge.uprojectを開きます。
ビルド補助はエンジン付属のProceduralMeshComponentをuprojectへ有効化し、
元のuprojectを `UEBridge.uproject.before-player-0.7.0` に保存します。
以前のC4458修正も含むため、古いパッチを後から上書きしないでください。

## 2. Minecraftで素材とスキンを書き出す

専用Minecraft環境を起動し、いつものクリエイティブのテストワールドへ入ります。
使用したい自分のスキンが読み込まれてから、それぞれ実行します。

```text
/uebridge textures export
/uebridge player export
```

テクスチャ書き出し完了のメッセージまで待ちます。ファイルは専用ゲームフォルダーの
`uebridge-export/textures-日時-識別子/` と `uebridge-export/player-日時-識別子/` に保存されます。
今回はブロック粒子のテクスチャ・色も加わるので、以前の素材が動いていても書き出し直してください。
スキンは現在のMinecraftが読み込んでいる画像を使い、標準/細い腕と透明な外側レイヤーに対応します。

## 3. UEへまとめて取り込む

UEでいつもの保存済みレベルを読み込みます。**Playを停止し、すべて保存**してください。
レベル内のBridgeReceiverは1個、GameModeはBridgeGameModeにします。

UEの出力ログ下の入力欄を **Python** に切り替え、次の2行を一行ずつ実行します。
最初は今回コピーしたimport_minecraft_player.pyの場所です。以下は現在使っている保存先の例。

```python
exec(open("C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/import_minecraft_player.py", encoding="utf-8").read())
setup_minecraft_visuals("C:/UEBridgeTest/MC-Test")
```

2行目は**専用Minecraftのゲームフォルダー**です。以前の書き出しが
`C:/UEBridgeTest/MC-Test/uebridge-export/...` にあった場合、この例をそのまま使えます。
別のゲームフォルダーなら2行目だけその場所に直してください。
`py` は付けず、パスは `/` を使います。日時や識別子を手で入力する必要はありません。

最新の完了済み書き出しを選び、ブロック素材・粒子素材・自分のスキンを取り込み、
BridgeReceiverへ割り当ててレベルを保存します。最後が **Minecraft visuals ready** になれば完了です。
途中でErrorがあった場合は最後の成功ログが出るまで未完了です。

既存のNiagara/Chaos設定は継続します。スキンを変更した際は `/uebridge player export` を実行して、
このPythonの2行を再実行します。単独で取り込む `import_minecraft_player("C:/.../manifest.json")` も使用できます。

## 4. 起動してUE操作へ切り替える

1. UEでPlay開始。Minecraftで `/uebridge status` の `UE=0.7.0` と `Avatar=true` を確認します。
2. Minecraftで飛行を止め、地面に立ちます。初期転送前に `/uebridge world off` を実行。
3. `/uebridge import start` → `/uebridge import` で **READY** まで待ちます。
4. `/uebridge control ue`。映像は全画面、移動・衝突・設置/破壊はUEが計算します。

視点切り替えは**Minecraft設定の「視点の切り替え」キー**を使います。
マウスサイドボタンへ割り当てている場合も、そのボタンで一人称→後方→前方→一人称となります。
F5を固定で読む実装ではありません。三人称のカメラは壁への衝突を確認し、
照準・設置/破壊は視点を変えてもキャラクターの目線から判定します。

音はMinecraft自身の音源と音量設定を使います。新たに音声ファイルを取り込む必要はありません。
通常歩行で余計な粒子は出ず、ダッシュでは足元のブロック粒子が出ます。
通常の小さなジャンプとクリエイティブの着地では、バニラ同様に架空の着地音を加えません。

## 5. テスト

- 一人称：空の主手で自分のスキンと袖を確認。ブロックを持ち、持ち替えと振りを確認。
- スキン設定：スリムなら腕幅が変わる。Minecraftのスキン設定で袖/帽子/上着を切り替えるとUE表示も変わる。
- 割り当てたキー/マウスボタンで3視点が切り替わり、三人称で全身が見える。
- 三人称で壁へ近づくとカメラが手前へ寄り、壁の内部を表示しない。左右上下は従来と同方向。
- 石/木/草ブロックを歩き、ブロックごとのバニラの足音が鳴る。しゃがみ歩行では足音を出さない。
- ブロック設置が成功したときに設置音、破壊が成功したときに破壊音とそのブロックの破片が出る。
  範囲外/占有/手の届かない場所への失敗した操作で成功音が鳴らないことも確認。
- ダッシュ時だけ足元に粒子。草の破壊粒子は緑一色でなく土のテクスチャ。
- `/uebridge control off` でMinecraftへ戻る。元のワールドはUE編集によって変更されない。

## 確認済みの範囲と制限

クラウドではMODのJava21ビルド・JUnit63件、Python21件が成功し、失敗/スキップ0。
実UDPのフィードバック往復・再送時の重複排除・不正パケットの拒否、スキン/テクスチャの検証を確認しました。
追加のUE Automationテストは用意しましたが、UE5.8がないクラウドではC++ビルド・描画・実機テストは未実行です。
0.6.0＋C4458修正までの実機動作はユーザー確認済みです。

主手の表示まで。オフハンド、ケープ、鎧、立方体以外の正確な持ち物モデル、
リアル水、追加TNT/専用音、Mob/HP、サバイバルの採掘時間/耐久/消費、UE地形のディスク保存は今後の範囲です。
使用中の手の姿勢は表現しますが、食事・弓などのゲーム効果はこの更新では追加しません。
音は基本ブロックのバニラSoundGroupを使用。積雪/複合ブロック/水泳など特殊な音分岐は未対応です。
粒子の画像・更新値はバニラを参照し、照明と衝突はUE方式です。破壊粒子は立方体の64個の分割が基本で、
細かい形状の分割や地点ごとのバイオーム色は未対応（素材書き出し地点の色を使用）です。
画像は既存のJPEG転送を継続し、遅延・FPSは今回改善済みと断定せずPCで測定してください。

将来の独自効果は音フィードバックの種類とUE側の粒子/VFX生成を追加できる構成です。
Minecraftの音・テクスチャ・個人スキンはユーザーのPCにだけ保存し、GitHub配布物に含めません。

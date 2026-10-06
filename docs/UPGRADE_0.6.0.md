# 0.6.0：アウトライン、UEでの設置・破壊、腕、映像転送の改善

Minecraftは入力とHUD、UEは描画と判定を担当します。今回の変更は次のとおりです。

- UEで5ブロック先まで照準を判定し、狙ったブロックに枠を表示。
- 左クリックで即時破壊、右クリックで選択中の通常の立方体ブロックを隣接マスに設置。長押しは0.2秒間隔。
- **UE側にブロック状の右腕と手を標準搭載**。手ぶらでも映り、歩行とクリックで動く。立方体の持ち物には既存パレットを適用。
- 歩行の加速・停止・空中操作を調整。Ctrl＋前進でスプリント。着地後の移動停止修正も含む。
- GPUの非同期読み戻し、Minecraftへの画素一括コピー、全画面時のMinecraft 3D描画省略。
- 映像設定を最大60fps・1080pまで拡張。入力からテクスチャ送出までの遅延と処理時間をstatusに表示。

## ダウンロードと更新

[MOD 0.6.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.6.0.jar)

[UE更新ZIP 0.6.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-update-0.6.0.zip)

**MODとUEを両方更新します。テクスチャの再インポートは不要です。**

1. Minecraftを終了。UEで今のレベルを保存し、UEとVisual Studioを閉じる。
2. テスト環境のmodsにある旧minecraft-ue-bridgeのJARを外し、0.6.0のJARを入れる。Fabric/API・Java21はそのまま。
3. UE更新ZIPを別フォルダに展開し、中の **Source、Build-UEBridge.cmd、Build-UEBridge.ps1、setup_world_bridge.py、import_minecraft_textures.py** を、いつも起動する`UEBridge.uproject`のあるフォルダへ上書きコピーする。
   Sourceは既存フォルダへ統合する。元のSource全体を削除しない。`Source/Source`という二重のフォルダにしない。
4. 例えば次の配置を確認する。使用中フォルダの名前が`UE-Minecraft-MVP-0.2.0`でも構わない。

   ```text
   UEBridge/
     UEBridge.uproject
     Build-UEBridge.cmd
     Build-UEBridge.ps1
     Source/
       UEBridge/
         BridgeCharacter.cpp
         BridgeVideo.cpp
         BridgeReceiver.cpp
         UEBridge.Build.cs
   ```

5. **Build-UEBridge.cmd**を実行し、`BUILD SUCCESSFUL`を確認する。Sourceをコピーしただけでは新しい処理は動かない。
   「Place this script next…」の場合は、同じ場所にuprojectと`Source/UEBridge/BridgeProtocol.h`があるか確認する。
6. 同じuprojectを開き、保存済みレベルを読み込む。GameModeはBridgeGameMode、PawnはBridgeCharacter、BridgeReceiverは1個、Texture Paletteは既存DA_MinecraftPaletteを使用。
   Content/Config/Savedは更新ZIPに含まれていないため、レベル・素材・Niagara設定を継続使用できる。

腕と枠はC++で作成するので、Blueprintで追加配置する必要はありません。既存のPreview Materialが色付き素材なら肌色と袖色になります。未設定でも標準の立方体メッシュを表示します。

## 起動

1. UEでPlay。Minecraftの専用クリエイティブ・シングルプレイを開く。
2. `/uebridge status`で **UE=0.6.0** を確認する。旧表示ならコピー先またはビルドしたuprojectが違う。
3. 飛行を止め、地面に立ち、しゃがみを解除する。
4. 次を順番に実行する。

   ```text
   /uebridge world off
   /uebridge world radius 1
   /uebridge import start
   ```

   `/uebridge import`でREADYになるまで待つ。`world off`は従来の継続転送を止め、初期転送の開始を妨げないようにするため。
5. READY後に次を実行する。

   ```text
   /uebridge control ue
   /uebridge video quality high
   /uebridge status
   ```

   UE判定=true / 保持=true、映像1280×720を確認する。最大60fpsの設定だが、実際のfpsはstatusに表示される。

UEの旧床・Landscapeが取り込んだ床と重なる場合は、専用レベルで**非表示と衝突OFF**を設定する。非表示だけでは衝突は残る。

## 操作とテスト

- **腕**：手ぶらで右下に腕と手が映る。石などの立方体ブロックを選ぶと持ち物が変わる。歩行・クリックで手が動く。
- **照準**：壁を狙うと枠が映る。照準はUEの位置・視線から計算する。草花や水など非衝突の形状は今回の照準対象外。
- **破壊**：枠のあるブロックを左クリック。表示と衝突を同時に除去する。階段など複数の直方体からなるブロックも、元の1マス分をまとめて除去する。
- **設置**：石・土・鉄など通常の立方体ブロックを選び、既存ブロックの面を右クリック。隣接マスに設置し、上に乗れることを確認する。自分の体と重なる場所・範囲外・既に埋まったマスには置けない。
- **移動**：WASD、Space、Shift、Ctrl＋前進。床に着地した後も歩行・ジャンプを繰り返せる。Space長押しで着地後に再ジャンプする。
- **保持**：`/uebridge control off`で戻り、`control ue`で再開。UEで設置・破壊した状態が同じPlay中に残ることを確認する。
- **操作失敗の理由**：`/uebridge status`の操作欄を見る。`no imported block in reach`は照準対象なし、`select a full cube block`は非対応の持ち物、`blocked by body or geometry`は体かUEの別の地形と重なっている。

**Minecraft側の元ワールドは変更しません。UEの地形変更はPlay中のメモリ内だけです。** Play停止・UE終了で消えるため、再起動後はimport startから実行する。import startの再実行でもUE地形を元ワールドから作り直す。

## 映像品質と遅延

| コマンド末尾 | 解像度 | 設定上限 | JPEG品質 |
|---|---:|---:|---:|
| `video quality low` | 480×270 | 30fps | 75 |
| `video quality balanced` | 960×540 | 60fps | 85 |
| `video quality high` | 1280×720 | 60fps | 90 |
| `video quality ultra` | 1920×1080 | 60fps | 90 |

いずれも先頭に`/uebridge`を付ける。highでfpsが低い場合はbalancedを試す。
全画面拡大しても、元の映像解像度より細かくはならない。60fpsは上限で、到達保証ではない。
UEは通常画面とは別のSceneCaptureで映像を作り、GPU読戻し→JPEG圧縮→TCP→復号→MCテクスチャ送出を行う。このためUE画面のfpsとMinecraft内の映像fpsは異なる。今回もJPEG方式で、GPU共有によるゼロコピーは未実装。

`/uebridge status`に次の値が表示される。

- **入力→upload**：表示する映像が使用したMinecraft入力の送信から、MCテクスチャのアップロード送出まで。同じMinecraft時計で測る。モニターの走査・VSync・実際のGPUアップロード完了は含まない。
- **GPU読戻し**：キャプチャ要求からGPUフェンスの確認と画素コピーまで。ポーリングを待つ時間も含む。
- **圧縮・復号・upload**：各CPU処理とアップロード送出の時間。RTTはUDP往復時間なので、映像遅延とは別。
- **MC描画省略=true**：全画面UE映像の受信中に、背後のMinecraftワールドを描画しない。HUD・インベントリ・チャット・入力・ワールド転送は継続する。映像が途絶えると通常の描画に戻る。

他の描画MODと組み合わせて不調がある場合は`/uebridge video optimize off`で省略を解除できる。再度有効にするには`optimize on`。
明るさは従来の`/uebridge video exposure 1`などで調整できる。
UEの背景CPU抑制が有効だと、Minecraft操作中のUE自体のfpsが下がる場合がある。以前の設定も維持して確認する。

## 今回の範囲

- 専用クリエイティブで即時編集。硬さ・採掘時間・ドロップ・UEとHUDの所持数同期は未実装。
- 腕は標準のブロック状モデル。プレイヤー固有のスキン取り込みは未実装。
- 立方体以外の持ち物は簡易の棒状表示。剣・弓など本来の形状やアニメーション、左手、F5三人称は未実装。
- 設置は通常の立方体のみ。階段・ドア・液体の設置、TNTへのUE側点火、弓、リアルな流体は次の段階。
- 特殊破壊用Blueprint入口Remove Imported Blocksは継続利用できる。通常の左クリックではNiagaraや破片は自動再生しない。
- 編集時には対象の8×8×8領域だけを更新する。大量・高頻度の編集をさらに軽くするメッシュ差分更新は今後の改善。
- 移動はMinecraftの速度・ジャンプ寸法に近づけたUE CharacterMovement。バニラ物理の完全再現ではない。

## 検証結果

Java21でMODビルド成功、JUnit47件・Python10件が成功（失敗・スキップ0）。
クリックの長押し/画面操作、UDP応答と入力時計、映像v1/v2・不正ヘッダ・JPEG色順、形状所有マスとパケット上限を検証。
Minecraft1.21.11の対象メソッドとNativeImageの画素バッファAPIを確認。
UEのパケット・地形編集Automationを追加したが、**クラウドにUE5.8がないためC++ビルド・GPU読戻し・腕/枠の描画・統合テストは未実行**。上記手順で実機確認が必要。

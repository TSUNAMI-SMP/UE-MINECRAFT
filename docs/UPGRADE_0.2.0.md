# 改良版 0.2.0

導入中の旧版ZIPは変更していません。0.2.0は別のZIPで提供します。
MinecraftのMODとUEプロジェクトを両方0.2.0に揃えてください。
同じMODの0.1.0と0.2.0を同時に `mods/` へ置かないでください。

## 今回追加したこと

| 機能 | 操作 / 状態 |
|---|---|
| 接続診断 | `/uebridge status`。応答、Camera/VFX設定、壁の数、RTT、未ACK/期限切れを表示 |
| BridgeのON/OFF | `/uebridge on` / `/uebridge off` |
| 同期原点を更新 | `/uebridge recenter`。次のフレームのMC足元を元のUE spawnFeetへ合わせる |
| ポート変更 | `/uebridge port 7780`。UE ReceiverのPortも同じ値にしてPlayを再開始 |
| 入力送信頻度 | `/uebridge rate 60`（20〜240Hz、実際は描画FPS以下） |
| 設定を読み直す | `/uebridge reload` |
| 周辺ブロックの手動表示 | `/uebridge preview`（接続とCamera準備完了が必要） |
| 周辺表示を消去 | `/uebridge preview clear` |
| 弓の試作を有効化 | `/uebridge bow on`。初期値OFF。`bow off` で無効化 |

すべてクライアント専用コマンドです。サーバーのコマンド権限や設定変更は不要です。

## 最初の接続診断

1. 新版UEのPlayを開始してからMCの専用ワールドへ入る。
2. `/uebridge status` を実行。
3. `UE応答あり` が通信確認。`Camera=true` はカメラ・Controllerが存在すること。
4. `VFX=true` はExplosion Systemが割り当て済み。`壁=1` 以上はタグ付きCollectionが存在すること。
5. これらは設定の確認で、見た目や破壊成功の保証ではありません。元の実機テストも続けてください。

RTTは、MC送信→UE応答→MCで受信処理するまでの時間です。描画/ポーリング待ちを含み、
実際に画面へ表示されるまでの遅延を測った値ではありません。
応答が1秒以上ないとMCは切断表示。UEの入力は250msで中立に戻ります。
UEを後から起動しても、ワールドを開き直さず応答を検出します。
旧版UEはstatus非対応なので、旧版との組み合わせでは応答待ち表示になります。

`config/minecraft-ue-bridge.json` に設定を保存します。設定が壊れていた場合は元ファイルを
保持してBridgeをOFFにし、ファイルを修正してからreloadします。ホスト変更は提供せずlocalhost限定です。

## TNT判定の改善

火打石/ファイヤーチャージでクリックしたTNTについて、同じ位置に着火済みTNT Entityが
サーバーから届いた場合に送信します。ブロックが消えただけでは送信しません。
複数のクリック候補を追跡し、期限切れ・ワールド退出時は破棄。Vanillaの使用を妨げません。
レッドストーン、連鎖爆発、他プレイヤー操作は依然として対象外です。
MCでTNT爆発が無効な場合はEntityが生まれずイベントを送信しません。

## 周辺ブロックの簡易プレビュー

1. UE BridgeReceiverの **Preview Material** を設定するとブロックの地図色を表示できます。
   未設定の場合はUE標準Cubeの単色表示です。
2. 用意済みの新規UEプロジェクトでは、`tools/setup_ue_demo.py` が床・PlayerStart・Receiver・
   色マテリアルを作成するエディタ用ヘルパーです。下の説明に従って実行できます。
3. 両ゲームを起動し、MCで `/uebridge preview`。
4. 足元を基準に、半径6ブロック・上下4ブロックの読み込み済みフルキューブを送ります。
   最大半径8まで設定可（最大2601キューブ）。
5. UEで全バッチが揃うと表示を一括更新します。欠落中は前の表示を保持します。
6. 建物を変更した場合は再度previewを実行。常時ストリーミング/自動差分同期ではありません。

表示用キューブには衝突・Chaos破壊を設定していません。独立した破壊壁とは別です。
Minecraftの全ワールド、階段/ハーフブロック等の形、液体、透明度、照明、テクスチャ、Mobは
再現しません。バニラリソースパックは不要で、地図色による粗いプレビューです。
1キューブ100cm、位置の対応は既存Bridgeと同じです。

バッチは最大12ブロック/2048byte以下。1tick最大4バッチ、ゲームイベント用に16枠を確保。
イベントの並べ替え・再送に対応し、途中のsnapshotは15秒で破棄。clear後の遅い旧バッチは無視。
通信切断で送信が失敗した場合はstatusで期限切れを確認し、接続復旧後にpreviewを再実行してください。

### UEの初期レベル作成ヘルパー

**UE EditorのPythonスクリプトです。PCの通常Pythonやクラウドでは実行しません。**
UEのC++を先にビルドし、Python Editor Script Plugin / Editor Scripting Utilitiesを有効にして再起動。
現在のマップを保存し、UEのコンソールで実行:

```text
py "C:/自分の保存先/UE-MINECRAFT/tools/setup_ue_demo.py"
```

この新規UEBridgeだけで実行できます。既存のBridgeDemoや保存前マップを上書きしません。
作成した `/Game/Maps/BridgeDemo` を開いてstartup mapへ設定してください。
Niagara爆発・Chaos破壊壁はこのヘルパーでは作らないため、`UE_SETUP.md` の設定が引き続き必要です。
このヘルパー自体もUE Editorのないクラウドでは実行未検証です。

## 弓の試作

`/uebridge bow on` を実行し、弓と矢を持って引いて放します。MinecraftのBowItemが使用成功を
返した場合、目の位置・向き・引き具合を送信します。UEはその位置から小さなコーン形の矢を
発射し、地形や壁で止めます（6秒で消去、停止後2秒、最大64本）。

これはUEで独立計算する簡易弾道です。MCの矢Entityの飛行/命中を継続同期しておらず、
Mob・HP・ダメージ・弓専用Niagara・壁の矢による破壊は未実装です。
Blueprintの `OnBowFired` から演出を追加でき、ReceiverのSpawn Bow Projectilesで標準矢を無効化できます。

## 検証と残り

- Fabric 1.21.11 / Java 21の新版MODビルド成功。
- Java 16件、Python 4件の自動テスト成功（skipなし）。
- UE側に3件のAutomationテストを追加。UEがないため **未実行**。
- UEテストはSession Frontend → Automation → `UEBridge.Protocol` で実行できます。
- UE 5.8 C++ビルド、ゲームGUI、プレビュー描画、弓、Niagara/Chaosの実機テストは **未検証**。
- Mob・HP同期、常時チャンク/ブロック差分同期、テクスチャ再現、破壊アセット自動生成は未実装。

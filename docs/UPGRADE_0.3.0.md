# 0.3.0：自動ワールド同期・UE映像・しゃがみ

Minecraft 1.21.11 / Fabric / Java 21はそのままです。**今回はMODとUEソースを両方更新します。**
クラウドでMODのコンパイル・自動テスト・TCP/JPEG通信を確認しています。
UE EditorのC++ビルド、実際の描画・色・しゃがみ・映像はWindows実機で確認が必要です。

## 導入

1. Minecraftを終了。UEで今のテストレベルを保存し、UEとVisual Studioを閉じる。
2. [MOD 0.3.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.3.0.jar)をダウンロード。
   テスト用Minecraftの`mods`から旧Bridge MODだけを取り出し、新しいJARを入れる。Fabric APIは残す。
3. [UE更新パッチ](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-update-0.3.0.zip)を別フォルダに展開。
4. 中の`Source`、`Build-UEBridge.cmd`、`Build-UEBridge.ps1`、`setup_world_bridge.py`を、
   **普段起動している`UEBridge.uproject`と同じフォルダ**にコピー。Sourceは上書きする。
   パッチはContent/Config/Savedを含まないため、作ったレベル・VFX・壁は保持される。
5. `Build-UEBridge.cmd`をダブルクリック。UEの場所はuprojectのEngineAssociationに対応するWindows登録情報から探す。
   自動検出できない場合、実際のUEインストール先（例：`C:\Program Files\Epic Games\UE_5.8`）を入力する。
   **BUILD SUCCESSFUL**が出たら、そのフォルダのuprojectを開く。失敗時は最初のerrorを確認する。
   Visual StudioのC++開発環境が必要。スクリプトは他プロジェクトを編集せず、起動中UEも終了させない。
6. 保存したテストレベルを読み込む。BridgeReceiverは1個、GameModeはBridgeGameModeを使用。
7. 色付きブロック用マテリアルを準備する。レベルを保存し、Playしていない状態でUEのコンソールに次を入力。

   ```text
   py "C:/実際の場所/UE-MINECRAFT/unreal/UEBridge/setup_world_bridge.py"
   ```

   このスクリプトは`/Game/Bridge/M_BridgeBlock`を必要時だけ作り、現在のレベルのBridgeReceiverに設定する。
   既存のPreview Materialがある場合は維持する。既存マテリアルにはVector Parameter `BlockColor`が必要。
   Pythonプラグインを無効にしている場合、Python Editor Script Pluginを有効にしてUEを再起動する。
   手動の場合：Materialを作成→Vector Parameterを追加して名前を`BlockColor`にする→Base Colorへ接続→
   BridgeReceiverのPreview Materialへ設定。
8. UEで通常のPlayを開始。Minecraftでワールドへ入り、`/uebridge status`の**UE=0.3.0**を確認する。
   `unknown`なら旧DLLが実行されている。新機能は旧UEへ大量送信しない。

PowerShellの実行ポリシーをPC全体で変更する必要はありません。cmdは同梱のビルドスクリプトをそのプロセス内で実行します。

## 最初のテスト

Minecraftで順番に実行：

```text
/uebridge world on
/uebridge video on
```

- 周囲のブロックがUEに順次表示され、Minecraft右上の小窓に**UEで別途描画した映像**が表示される。
- `/uebridge world`で送信確認済み領域数を確認。空気だけの領域も数える。
- 初回転送は数秒〜数十秒かかる。近い領域から送信する。静止しても未ロード領域や変化を再確認する。
- ブロックを設置・破壊する。同じ領域が更新され、UEの形状も変わる。通常は秒単位の更新で、描画フレームごとの即時反映ではない。
- Shiftでしゃがむ。UEの目線がMinecraftと同じ高さまで下がり、離すと戻る。
- 移動して8ブロック単位の境界を越えると、新しい周辺領域を同期し、遠い領域をUEから取り除く。
- UEでPlayを終了→再度Play。接続復帰後にワールドが再送信される。
- ディメンション変更・ワールド退出・`recenter`で原点と同期状態をリセットする。

既存のUEテスト用の大きな床が同期地形を覆う場合は、その床の**Actor Hidden In Game**をONにして表示を退避する。
同期ブロックは見た目専用で衝突を持たず、プレイヤー位置・衝突判定はMinecraft側が決める。
Niagara・Chaosの割り当てはこれまでのレベル設定を使用する。TNTによるMinecraftの地形変化はワールド同期にも反映されるが、
同期された各ブロック自体をUEのChaos破壊アセットへ自動変換する機能は含まない。

## 映像の表示切り替え

```text
/uebridge video fullscreen
/uebridge video pip
/uebridge video off
```

全画面はMinecraftの3D景色の上にUE映像を描き、クロスヘア・ホットバー・チャットを残す。
Minecraftの入力をそのまま使い、UE側の描画・VFXを見ながら操作できる。
これはHUDへの映像表示で、Minecraft内の額縁・ブロック面に映像を貼る機能ではない。
音声は転送しない。Minecraft自体の描画負荷も残る。

初期映像設定は480×270 / 最大15fps / JPEG品質75。UEのBridgeReceiver→Videoコンポーネントで
Width/Height/Frames Per Second/Qualityを変更できる。映像と入力の更新頻度は別。
映像にはSceneCaptureの追加描画・GPU読み戻し・圧縮・転送・デコードによる遅延がある。
圧縮/デコードは別スレッド、滞留時は次のフレームを溜めず送信を抑制するが、GPU読み戻し自体は同期処理。
重い場合は解像度・fpsを下げる。最終品質の低遅延GPU共有方式ではなく、依存の少ない試作方式。

映像待ちの場合：UEでPlay中か、statusでUE=0.3.0か、VideoPortが両側7780か確認。
変更はMinecraft側`/uebridge video port 7780`、UE側BridgeReceiverのVideo Port。
通信は同一PCの127.0.0.1のみ。外部IPへの公開やポート開放は不要。
UEがバックグラウンドで数fpsになる場合、Use Less CPU when in BackgroundをOFFにする。

## ワールド同期の操作と範囲

```text
/uebridge world on
/uebridge world off
/uebridge world refresh
/uebridge world radius 1
/uebridge world radius 2
/uebridge world radius 3
```

設定は`config/minecraft-ue-bridge.json`へ保存する。最初はworldSync=false、videoMode=0。
一度ONにすると次回も自動接続・同期する。再送信したい場合はrefreshを使う。

8×8×8ブロックを1領域とし、既定radius=2 / worldHalfHeight=1で横40×40・縦24ブロック。
範囲は8ブロック境界に揃い、プレイヤー中心に厳密な対称ではない。
radius=1は横24×24、radius=3は横56×56。高さは設定ファイルのworldHalfHeight=1〜2（縦24〜40）。
セルごと最大8192形状、UE全体最大131072形状。上限を超えた場合は送信期限切れになり、範囲を小さくする必要がある。

フルブロック、階段・ハーフブロック・柵などのOutline Shapeを直方体群へ変換する。
水は概略形状で表示。色はMap Colorであり、バニラテクスチャ、葉・水の透過、草花の板ポリゴン、
チェスト等のBlockEntity描画、Minecraftの照明、Mobは再現しない。
全セーブデータを一括変換する方式ではなく、**Minecraftクライアントがロードしている周辺を継続同期する方式**。
遠くの全ワールドはロードしない。大きい建築は範囲設定を広げるか移動して確認する。
建築ブロックをUEの永続アセットとして保存する機能はまだない。Play終了で同期表示は消える。

## 開発の構成

- `WorldSnapshot.java`：ゲームに依存しないセル・形状・分割通信・SHA256差分判定。
- `WorldSync.java`：ロード済みブロックの分割走査、変更領域優先、ACK確認後の差分キャッシュ。
- `WorldUpdateMixin.java`：クライアントのブロック変更通知。定期走査がチャンク読み込みも補完。
- `BridgeWorld.*`：領域の世代管理・完全受信後の置き換え・遠い領域の除去。
- `BridgeVideo.*` / `VideoProtocol.java` / `VideoClient.java` / `VideoOverlay.java`：独立した映像経路。
- `BridgeCharacter.*`：Minecraftの体高・目線・しゃがみ状態を適用。

後でブロックID/状態・テクスチャパレット・チャンク用メッシュ生成・永続化を追加できる境界に分けている。
今のパケットにテクスチャデータやMinecraftゲームオブジェクトを含める必要はない。

## 確認できた範囲

クラウド：Java 21でMODビルド、既存テスト、新しいセル分割/差分・JPEG検証・実TCP受信テスト、Python診断テスト。
UE：C++ビルド、Protocol Automation、Editor Pythonスクリプト、Minecraft/UE実描画の統合テストは未実施。
Windowsで上記テストを行い、失敗した場合はUEの最初のコンパイルエラー、latest.log、statusを確認する。

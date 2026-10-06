# 起動・テスト

0.3.0のワールド・映像・しゃがみの導入/実機テストは [UPGRADE_0.3.0.md](UPGRADE_0.3.0.md)。

## 先に必要なこと

- JDK 21でMODをビルドし、1.21.11用のFabric Loader/APIとともに専用Launcher環境へ配置。
- UE 5.8でC++をビルドし、[UE_SETUP.md](UE_SETUP.md) のデモレベル/VFX/壁を設定。
- MinecraftとUEは **同じPC** で起動。クラウドで動かす送信ツールからPCのlocalhostへは届きません。

## 最初の成功条件

1. UE Playを開始。Output Logに `Bridge 0.3.0 listening on 127.0.0.1:7779` が出ること。
   `ExplosionSystem is unset` やTargetCharacter不在のエラーを解消する。
2. Minecraftの新しいクリエイティブワールドへ入る。
3. Minecraftのマウスで左右・上下を向く。UEカメラが同方向へ追従すること。
4. MCで前後左右に歩く。1ブロック移動がUE100cmになること。
5. MCでジャンプ。UEのCharacter/カメラ高さが追従すること。
6. UE壁に対応するMC位置へTNTを置き、火打石で着火。
7. UEで同じ場所にNiagaraバーストが一度だけ再生され、Chaos壁が破片になり飛ぶこと。
   Logの `affected wall collections` が1以上になること。
8. 壁を復元して再実行。連続TNTイベントが別IDで各1回処理されること。
9. MCから退出。UEが最後の位置で停止すること。両方を再起動して再接続できること。

3と7が両方目視確認できて初めて今回の成功条件を満たします。
UDP疎通・ACK・Javaのビルド成功だけではこの条件を満たしません。

## MOD送信だけを切り分け

UE Playを止めてから、同じPCでPython 3を使う:

```sh
python bridge/smoke.py listen --seconds 30
```

MCで視点を動かしTNTを着火。input packetsが0でなく、last inputにyaw/pitchが含まれ、
unique eventsが増えること。これはMOD→UDPの確認で、UEの確認ではありません。0.2.0ではMCに「UDP診断ツール」と表示されます。
終了したらUE Playを再開します。このツールとUEは同じ7779ポートを同時には使えません。

## UE受信だけを切り分け

MCワールドから退出して、UE Playを開始。同じPCで:

```sh
python bridge/smoke.py send --seconds 3 --tnt
```

合成Yawがカメラを回し、UE spawnFeetから `(350,-50,50)` cm の位置で爆発します。
ACKが返ることに加え、Niagaraと壁を目視確認。テスト後はUE Playを再開始してMCへ接続。

## 自動チェック（クラウドで実行済み）

```sh
python3 tools/build_mod.py build
python3 -m unittest discover -s bridge/tests -v
```

Gradle/JUnitは実際のJava BridgeTransportとloopback UDP socketを使い、入力送信、
イベント再送、正しいACKで停止、不一致sessionのACKを無視、2048byte超の拒否を確認。
Pythonはテストツール自身のloopback往復を確認します。UE実装の代替テストではありません。

## よくある原因

- Javaのコンパイル失敗：JREでなくJDK 21か、JAVA_HOMEが正しいか確認。
- UE起動時にmoduleエラー：UEバージョンとC++ビルドツールを確認。
- bind失敗：UE Receiverが複数、複数PIE、またはsmoke listenが同じポートを使用。
- カメラが動かない：GameModeがBridgeGameModeか、通常Playか、専用MODを読み込んだか確認。
- ACKだけ返る：Niagara未設定、普通のStaticMesh、Actorタグなし、壁が爆発半径外を確認。
- 数秒遅い：両ゲームを同時表示し、非アクティブウィンドウのFPS制限を確認。
  Minecraftの描画FPSが送信頻度を制限し、UE側の描画FPSも反映速度を制限します。

## 未検証

UE 5.8のC++ビルド、Minecraft GUI起動、UEカメラ追従、実アセットによるNiagara/Chaos、
周辺ブロックプレビュー、弓の描画、実機の遅延測定。GUI/UEがないクラウドの制限です。
0.2.0の追加テストとコマンドは [UPGRADE_0.2.0.md](UPGRADE_0.2.0.md) を参照してください。

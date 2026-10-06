# 0.4.0：ブロックテクスチャと映像品質

Minecraft 1.21.11 / Java 21はそのまま。**MODとUEソースを両方更新**します。
0.3.0のワールド/映像はユーザー実機で動作確認済み。今回のUE C++・Python・映像品質は実機確認が必要です。

## 1. 今のプロジェクトを更新

1. Minecraftを終了。UEで使用中のテストレベルを保存し、UEとVisual Studioを閉じる。
2. [MOD 0.4.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.4.0.jar)をダウンロード。
   テスト用Minecraftの`mods`の旧Bridge JARだけを取り出し、これに交換。Fabric APIは残す。
3. [UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-update-0.4.0.zip)を別の場所へ展開。
4. ZIPの`Source`、`Build-UEBridge.cmd`、`Build-UEBridge.ps1`、`setup_world_bridge.py`、`import_minecraft_textures.py`を、
   **いつも開いているUEBridge.uprojectと同じフォルダ**へ上書きコピー。
   Content/Config/Savedを含まないので、保存したレベル・壁・VFXは保持される。フォルダ名が0.2.0でも問題ありません。
5. そのフォルダの`Build-UEBridge.cmd`を実行。**BUILD SUCCESSFUL**を確認してからuprojectを開く。
   Sourceの上書きだけでは新しい処理は動きません。
6. 保存したテストレベルを読み込む。BridgeReceiverは1個、BridgeGameModeを使用。
   既に色付き地形が動いていた場合、`setup_world_bridge.py`の再実行は不要。
   色用マテリアルが未設定の場合だけ、[0.3.0手順](UPGRADE_0.3.0.md)のPythonコマンドで準備する。

## 2. Minecraftのテクスチャを書き出す

1. Minecraftを起動し、使いたいリソースパックを適用して、テストワールドへ入る。バニラのままでも使用可能。
2. チャットで実行：

   ```text
   /uebridge textures export
   ```

3. 別スレッドで処理する。`/uebridge textures`で進捗を確認。完了メッセージに`manifest.json`の保存先が出る。
4. 保存先は**このMinecraftインストールのゲームディレクトリ**の
   `uebridge-export/textures-日時-識別子/manifest.json`。
   専用ゲームディレクトリならその中にあり、標準の場合は`%APPDATA%/.minecraft`内にある。
   `manifest.json`と`assets`フォルダは同じ場所に置いたまま使う。

バニラリソースパックをこちらへ提出する必要はありません。現在のリソースからPC内に書き出します。
ワールドや元のリソースは変更しません。毎回別フォルダを作ります。
リソースパックを変更したら、再度exportして新しいmanifestを取り込んでください。
書き出した素材はGitHubの配布物には含めません。

## 3. UEへ取り込む（素材変更時に一度）

1. UEのPlayを停止して、現在のテストレベルを保存。
2. Python Editor Script Pluginが有効な状態で、コンソール入力欄を**Python**にする。
3. 次の1行の**2つのパスを実際の場所へ変更**して実行する。Windowsでも`/`を使う。
   `py`は先頭に付けない。

   ```python
   exec(open("C:/実際のUEBridgeフォルダ/import_minecraft_textures.py", encoding="utf-8").read()); import_minecraft_textures("C:/実際のMinecraftゲームフォルダ/uebridge-export/textures-日時-識別子/manifest.json")
   ```

4. テクスチャ、上/横/下面用マテリアル、ブロックID対応表を`/Game/Bridge/Minecraft`へ生成し、
   現在のBridgeReceiverの**Texture Palette**に割り当ててレベルを保存する。
   同じ内容の再取り込みでは生成済み素材を再利用する。処理は数分かかる場合がある。
5. Output Logに`Minecraft texture palette ready`が出たら、通常のPlayを開始。
6. Minecraftで実行：

   ```text
   /uebridge status
   /uebridge world on
   /uebridge world refresh
   /uebridge video on
   ```

`UE=0.4.0`、`textures`の数が0より大きいことを確認。ワールドの初回同期完了まで待つ。
取り込みが完了した同じ保存レベルなら、次回起動時はimport不要です。
別のテストレベルで使う場合は、そのレベルのBridgeReceiverのTexture Paletteに
`DA_MinecraftPalette`を指定して保存すれば共有できます。

## 4. 映像を調整

```text
/uebridge video quality balanced
/uebridge video fullscreen
/uebridge video exposure 1
```

|品質|解像度|目標上限fps|JPEG品質|
|---|---|---|---|
|low|480×270|15|75|
|balanced（既定）|960×540|20|85|
|high|1280×720|30|90|

`/uebridge video quality high`で高品質、重ければ`low`へ戻す。
露出は`-6`〜`6`。暗ければまず`1`、明るすぎれば`0`や`-1`を試す。
`0`は追加の露出補正なし。映像用SceneCaptureの露出履歴を保持し、出力ガンマを明示する変更も含む。
設定はMinecraftの`config/minecraft-ue-bridge.json`へ保存し、接続先UEへ再適用する。
`/uebridge status`で受信画像の解像度・実FPS・品質設定・露出を確認できる。
実FPSは両ゲームの負荷によって目標上限を下回る。入力同期は映像とは別に高頻度で継続する。

SceneCaptureとUEの画面ではLumen・ポストプロセス等により明るさや描画が完全一致しない場合がある。
GPU読み戻し＋JPEGの試作経路を継続しており、1080p/60fpsやGPU共有は今回含まない。
UEが非アクティブ時に低FPSになる場合はUse Less CPU when in BackgroundをOFFにする。

## 5. 成功確認

- 石・土・草ブロック・オークの板材・原木・レンガを並べ、UE側に各テクスチャが表示される。
- 草ブロックの上面と下面が違う画像になり、石や板材は不必要な色補正を受けない。
- 設置・破壊後に同期が更新される。しゃがみ・左右視点・TNTの従来動作も確認する。
- `quality high`で受信解像度が1280×720へ変わり、`exposure 1`で映像の明るさが変化する。
- UE Playの再起動後に設定とワールドが再送信される。

同期地形が見えず元のUEの床だけ見える場合、その床のActor Hidden In GameをONにして試す。
全画面はHUD上への表示で、MC自体の景色の描画負荷も残る。

## 現段階の対応範囲

ブロックの**デフォルト状態**から上・側・下面の画像を抽出し、同期された直方体形状へ貼る。
バニラの761種類について素材解決・書き出し・取り込み前検証をクラウドで確認した
（variantsを持つ1072種の各先頭状態を使う単独リソース試験。実際の全デフォルト状態の確認ではない）。

向きごとの全側面、状態ごとの絵柄、元モデルのUV/回転/重ね描き、multipartモデル、
葉・ガラス・水の透過、BlockEntity、Mobは完全再現しない。
アニメーションテクスチャは先頭タイルの静止画像。草/葉の色は既存Map Colorの近似でバイオーム色とは異なる。
非対応のブロックは従来の色付き表示へ戻る。テクスチャパックによって対応数は異なる。
ワールド全体の一括保存、永続地形メッシュ生成、同期ブロック自体のChaos化も今後の段階。

## 検証結果

Java 21によるMODビルド、JUnit 35件、Python 10件成功。
実Minecraft 1.21.11リソースを使った単独書き出し：761ブロック/575テクスチャ、manifestのPNG/パス/SHA256検証成功。
UEのProtocol Automationを2件追加したが、クラウドにはUEがないため未実行。
UE 5.8でのC++ビルド、Pythonアセット生成、両ゲームでの表示品質はWindows実機で確認が必要。

# UEBridge 0.18.4：マスへの設置・音・影と物理表示の修正

0.18.1〜0.18.3の取り込み修正を含むUE専用の累積更新です。MODは **0.18.0** を使います。0.18.0の `native_manifest.json` を再利用でき、Minecraftからの再書き出しは不要です。0.17.0未導入からの初回導入は [0.18.0導入手順](UPGRADE_0.18.0.md) を使い、UE更新には本ZIPを適用してください。

## 導入

1. UEを終了し、既存のUEBridgeフォルダーをバックアップします。
2. [UEBridge-update-0.18.4.zip](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.18.4.zip) の**中身すべて**を既存の `unreal\UEBridge` フォルダーへ上書きします。ZIP自体を置くだけでは更新されません。
3. そのUEBridgeフォルダーでPowerShellを開き、次を実行します。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Play-Native.ps1" -Rebuild -Reimport
```

選択画面で、今使っている0.18.0の `native_manifest.json` を選びます。**今回はC++再ビルドと素材再取り込みの両方が必要です。** Content・Saved・保存ワールドは削除しません。専用素材は `/Game/Bridge/Realistic` の `_v2` 名で生成します。

## 修正内容

| 対象 | 更新後の処理 |
|---|---|
| マスへの設置 | 狙った面に隣接するMinecraftの1マスへ砂・TNT・水・溶岩を配置。負の座標と小数の書き出し原点にも対応。地形や既存の物理に重なる配置は拒否し、アイテムを保持します。砂や液体は配置後に動きます。 |
| TNT | 未着火時はマスの位置を保持。着火後は重力・導火線・爆発を処理。 |
| 音 | 砂の設置・崩落、TNTの設置・着火・爆発、水／溶岩バケツの使用・回収、液体の環境音を追加。音源は既存の書き出しに含まれるMinecraft音です。 |
| 砂 | 大きな球の表示を小さな角粒へ変更。対称な柱状のまま残る状態を崩し、粒の押し合いで床へ入り込むケースを修正。 |
| 水・溶岩 | 設置された物理にはMinecraft画像を使わず、透過・屈折・水面法線、溶融した溶岩と暗い表面を専用の手続き型マテリアルで生成。 |
| 爆発 | 対応するNiagara Fluids爆発テンプレートがUEに入っていれば検出・複製して使用。未導入なら専用の火・煙素材を使用し、ログに表示。 |
| 影 | 照明と動的な影を分離。影は既定OFF。ON時の太陽・月の光源角も調整し、AOは無効にしています。 |
| CPU負荷 | ブロック照会を全行走査から索引へ変更。草の対象セクションの毎tickソート、空の物理更新、静止した粒・TNTの表示更新、変化していない液体メッシュの再生成を削減。 |

手持ちモデルとインベントリアイコンは引き続き既存のモデルを使用します。専用素材への変更対象は、設置した物理と爆発の表示です。

## 操作・物理の削除

Escメニューに「影 ON/OFF」「性能を確認」を追加しました。書き出し済みフォントに新しい漢字がない場合は「Shadows」「Performance」と表示します。`T` または `/` のチャットからも使えます。

```text
/shadows off
/shadows on
/realistic on
/realistic off
/realistic quality low
/realistic quality medium
/realistic quality high
/realistic items
/diagnostics
/physics status
/physics clear sand 16
/physics clear water 32
/physics clear lava 32
/physics clear tnt 16
/physics clear rock 16
/physics clear all all
/physics undo
```

削除範囲の数値はプレイヤーからの半径をブロック単位で指定します。`all all` は保存中の全物理を削除します。通常の地形は対象外です。`/physics undo` は直前の削除を復元しますが、再起動や新しい物理配置・着火・爆発後には使えません。

0.18.3以前の砂とTNTは元の位置を保持します。古い液体と固化物は水量を保って新しい格子へ移し替え、位置を各軸で最大12.5cm補正します。マス配置を確認するときは新しく設置してください。古い物理をまとめて片づける場合は `/physics clear all all` を使用します。

## FPSとNiagaraの確認

`/diagnostics` の詳細は `Saved\Logs\UEBridge.log` に出ます。`worldTickMs` は地形・規則・光、`solverMs` は物理計算、`physicalMeshMs` はCPU側の物理表示更新です。GPU全体や他のActorの時間は含みません。UEコンソールの `stat unit` でGame／Draw／GPU時間も確認すると、残る負荷を切り分けられます。画像だけでは30fpsの原因を確定できず、今回の高速化による実FPSの改善幅は未検証です。

Niagara Fluidsがエンジンにインストールされていれば、ビルド時にプラグインを有効化します。取り込みログの `Realistic explosion Niagara template:` が検出成功です。`Realistic Niagara explosion template unavailable` なら代替表示です。所有するNiagara Systemを `/Game/Bridge/Realistic/NS_RealisticExplosion` に置く場合、そのアセットを保持します。テンプレート固有のパラメーターによっては個別調整が必要です。

## 現時点の範囲と検証

これは物理表示の改善版であり、動画のような撮影用流体の完成版ではありません。砂は1個につき512個の衝突用粒子で計算し、medium／highでは周りに計4096個の細粒を表示します。すべての細粒を独立計算する仕組みではありません。水・溶岩は引き続き25cm格子の有限体積計算で、SPH／FLIPではなく、滑らかな表面の再構成や熱・圧力の完全な連成は未実装です。専用素材も写真素材ではなく独自の手続き型表現です。

Linux上の独立C++・Python・PowerShell検証と配布ZIPの内容検証を行っています。Windows UE 5.8.3のビルド、HLSLコンパイル、Niagaraの実再生、音・入力・GPU描画と実FPSはこの環境では確認できません。[検証記録](AUDIT_0.18.4.md) を参照してください。

# UEBridge 0.18.0 導入・更新手順

Minecraft Java 1.21.11から書き出し、UE 5.8で単独プレイする版です。**0.17.0を先に導入する必要はありません。** 0.17.0までの描画・操作・クラフト・装備・基本回路の変更を含み、UE専用の物理アイテム4種類、表示切替、削除と取り消し、60fps動画書き出しを追加します。

- [MOD 0.18.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/minecraft-ue-bridge-0.18.0.jar)
- [使用中のUEBridgeへ適用する更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.18.0.zip)
- [初回導入用のソース・MOD一式](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UE-Minecraft-MVP-0.18.0.zip)
- [検証記録](AUDIT_0.18.0.md)／[既存機能の対応範囲](NATIVE_PLAY.md)

## 0.15.x／0.16.xなどから直接更新する

1. UEとMinecraftを終了します。使用中のUEBridgeフォルダーとMinecraftの元ワールドをバックアップします。特にUEの `Content` と `Saved`、書き出し元のフォルダーを残してください。
2. Minecraftの `mods` から旧 `minecraft-ue-bridge-*.jar` を外し、**0.18.0のJARを1個だけ**入れます。Minecraft Java 1.21.11＋Fabric＋Java 21を使用します。
3. Minecraftで元ワールドを開き、`/uebridge native export` を実行します。完了した新しいフォルダー内の `native_manifest.json` を使います。今回の4アイテムと物理用素材には**0.18.0での新しい書き出しが必要**です。
4. 更新ZIPを展開し、その**全内容**を使用中の `UEBridge.uproject` の隣へ上書きします。`Source`、15個のPythonヘルパー、起動スクリプトをまとめて更新します。`Content`／`Saved`を削除する手順ではありません。
5. `UEBridge.uproject` のあるフォルダーでPowerShellを開き、次を実行します。ファイル選択画面で手順3の **新しい** `native_manifest.json` を選びます。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Play-Native.ps1" -Rebuild -Reimport
```

例えば現在の配置が以前と同じなら、先に次で移動できます。

```powershell
Set-Location -LiteralPath 'C:\Users\tsush\Documents\UE-Minecraft-MVP-0.15.0\UE-MINECRAFT\unreal\UEBridge'
```

このフォルダー名が0.15.0のままでも、全ファイルを上書きすれば使用できます。最初はC++ビルドと素材取り込みで時間がかかります。次回からは `Play-Native.cmd` のダブルクリックで再開します。書き出し後のMinecraftは終了して構いません。

新規書き出しには別の識別子が付くため、以前のUE編集状態を自動で合成することはできません。旧書き出しと `Saved/NativeWorlds` を残すと旧パッケージを再選択して旧保存を開けます。旧書き出しには今回のアイテムと素材が含まれません。

## 初回導入する

1. Minecraft Java 1.21.11、対応FabricとFabric API、Java 21、UE 5.8、Visual Studio 2022のC++ゲーム開発環境とWindows SDKを用意します。詳しいUEの準備は [初回導入ガイド](INSTALL_0.15.0_JA.md) を参照します。
2. 一式ZIPを展開します。MODは `UE-MINECRAFT/artifacts/minecraft-ue-bridge-0.18.0.jar` です。これをMinecraftの `mods` に入れ、上記の手順3を行います。
3. `UE-MINECRAFT/unreal/UEBridge/Play-Native.cmd` を起動し、新しい `native_manifest.json` を選びます。初回ビルド・素材取り込み・ゲーム起動が始まります。

一式ZIPにはUEのソースと起動ツールが入っています。WindowsでC++をビルドする必要があります。Minecraftの画像・元のクラス・外部の動画素材は同梱していません。

## 4アイテムの使い方

UE内で `T` または `/` からコマンドを入力し、`/realistic items` で4種類を各1個取得できます。クリエイティブの検索欄では「リアリスティック」で探せます。インベントリに空きを作ってください。識別子指定の `/give` でも取得できます。

アイコンと手持ちモデルは元の砂・TNT・バケツを共用し、名前と識別子を分けています。設置後に今回の物理表示になります。Minecraft側へ新しい登録アイテムを追加する構成ではありません。

| アイテム | 識別子 | 操作・動作 |
| --- | --- | --- |
| リアリスティック砂 | `uebridge:realistic_sand` | 地形を狙って右クリック。1個を512粒の砂にし、重力・接触・積み上がり・支持を失った崩落を計算 |
| リアリスティックTNT | `uebridge:realistic_tnt` | 地形を狙って右クリックで置く。火打石と打ち金またはファイヤーチャージで右クリックして着火。4秒後に爆発し、砂・液滴・他の物理TNTへ力を加える |
| リアリスティック水入りバケツ | `uebridge:realistic_water_bucket` | 地形を狙って右クリック。有限量の水を注ぎ、流れ・波・泡・爆発時の飛沫を表示 |
| リアリスティック溶岩入りバケツ | `uebridge:realistic_lava_bucket` | 右クリックで有限量の溶岩を注ぐ。水より遅く流れ、水との接触で固化。プレイヤーの溶岩ダメージ判定にも使う |

サバイバルでは成功時だけ所持品を消費し、液体を注ぐと空バケツを返します。空の通常バケツで物理液体を右クリックすると、つながった液体が1杯分以上ある場合に正確に1杯回収します。回収先が満杯なら液体と所持品を元へ戻します。通常のバニラ水・溶岩と、この有限量の液体は別のものです。

1杯は1立方メートル、液体の計算格子は25cmです。空間・取り込み範囲・個数上限に達した配置は失敗し、アイテムを保持します。上限は砂8192粒、液体と固化セルの合計16384、物理TNT64個、液滴1024個です。削除すると枠が空きます。

物理TNTは近くの地形を削除し、遮蔽と距離に応じてモブとサバイバルのプレイヤーへダメージ・押し出しを加えます。バニラの爆発耐性・戦利品・火災・すべての連鎖処理の完全再現ではありません。

## 見た目と負荷の切替

Escのゲームメニューに「リアル表示」「品質」を追加しています。コマンドでも変更できます。

```text
/realistic on
/realistic off
/realistic quality low
/realistic quality medium
/realistic quality high
/physics status
```

既定はリアル表示ON・品質mediumです。ONでは物理アイテムの光・水の反射／屈折用マテリアル・波・泡・発光を使います。OFFでは同じ物理を簡易マテリアルで表示します。品質で波と爆発描画の密度を変更し、砂・液体量・TNT導火線の計算は変更しません。バニラ地形の照明は既存のF6またはメニュー「照明」で別に変更できます。

この版の水は有限量の格子計算と手続き的な波、爆発は火・煙のメッシュ表現です。SPH／FLIP液体、体積煙、動画例のBlender品質に達した実装ではありません。表示の改善を重ねられる基盤として追加しています。実プレイの60fps維持は保証せず、実機で品質と物理量を調整します。

## 使用しなくなった物理を消す

Escの「近距離の物理を削除」は、プレイヤーの周囲16ブロックにある今回の砂・液体・物理TNT・固化セル・液滴を削除します。「直前の削除を戻す」で取り消せます。

種類・範囲を指定する場合は次を使います。

```text
/physics clear sand 16
/physics clear water 32
/physics clear lava 32
/physics clear tnt 16
/physics clear rock 16
/physics clear all 16
/physics clear all all
/physics undo
/save
```

最後の `all all` は取り込み範囲内の今回の物理をすべて消します。通常の地形・バニラの砂や水・所持品は削除しません。点火中の物理TNTも削除でき、削除後は爆発しません。火・煙の一時エフェクトは5秒以内に自動で消えます。

取り消しは**直前の削除時点の物理全体**を戻す操作です。成功した配置・回収・着火、物理爆発、再起動で取り消し用の記録は無効になります。通常の時間経過は一緒に巻き戻りますが、爆発で既に削除された通常地形を戻す機能ではありません。削除後は `/save` またはメニューの保存で確定できます。砂・液体・固化物・導火線・表示設定は通常のUE保存に含まれます。

## プレイが低fpsでも60fpsで動画を書き出す

1. UEで `/record start`、またはEscの「動画の記録を開始」を選びます。開始時にワールドを保存し、録画用の独立した基準ワールドをコピーします。メニューを閉じてプレイします。
2. `/record stop` または「動画の記録を停止」を選びます。`/record status`、メニュー「動画の保存先を確認」で保存場所を表示できます。
3. ゲームとUEエディターを終了し、`UEBridge.uproject` の隣の **`Render-Replay.cmd`** を起動します。
4. `Saved/Cinematics/日時-識別子/replay.json` を選びます。画面をリサイズせず、書き出し終了を待ちます。
5. 同じ録画フォルダーに **`video-60fps.avi`** が生成されます。追加のエンコーダー導入は不要です。

解像度を指定する場合：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Render-Replay.ps1" -Width 1920 -Height 1080
```

20Hzを上限にカメラ・プレイヤーと手の形状・モブ・投下物・地形変更・今回の物理と爆発を記録し、再生時に1/60秒ごとの補間された状態を新しく描画します。保存ワールドを上書きしません。同じ書き出しパッケージの素材が必要なので、別パッケージを取り込んだ場合は録画に使用した書き出しを再選択してください。

書き出しは実時間より遅くても、AVIを60fpsの時間軸で作れます。ただし、低fps時に観測できなかった入力や細かな接触を復元するものではありません。全GUI操作・バニラの全粒子・矢・通常の落下ブロックやTNTなど全エンティティの録画は対象外です。今回の物理アイテムを使う短いシーンが対象です。

出力は**無音のMJPEG AVI・通常のSDR**です。録画は最大120秒／512MiB、AVIは約1.9GiBの上限があります。高解像度・動きの多い場面では先に容量上限へ達するため、まず720pで10〜30秒程度を試してください。容量上限・GPU読み戻し失敗・ウィンドウサイズ変更時は未完成の `.part` とログを残し、成功した動画として扱いません。

## 導入後の確認とログ

まず少量の砂と水を置き、物理TNTを1個着火して、移動・飛沫・削除・取り消しを確認します。表示ON/OFFの比較、保存・終了・再開、短い録画の60fps書き出しを順に確認します。

- C++ビルド：コンソールの最初の `error C...` または `error:`。
- 素材取り込み：`UEBridge/Saved/Logs/NativeImport-*.log`。
- プレイ：`UEBridge/Saved/Logs/UEBridge.log`。
- 動画：`UEBridge/Saved/Logs/NativeReplay-*.log`。成功時は `Bridge cinematic COMPLETE:` が出ます。
- 物理素材不足：`Bridge realistic material missing`。0.18.0の新規書き出しと全ヘルパーの上書き後、`-Rebuild -Reimport` を行います。

Java・Python・独立C++・PowerShell関数・AVI形式を検証しています。**今回のWindows UE 5.8.3のコンパイル、実取り込み、シェーダーとGPU描画、統合プレイ、実fpsは未検証**です。結果と再実行方法は [AUDIT_0.18.0.md](AUDIT_0.18.0.md) に記載します。

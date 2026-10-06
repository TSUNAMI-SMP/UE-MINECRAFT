# ダウンロード

## 最新版 0.7.0

[MOD 0.7.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.7.0.jar)

[UE更新ZIP 0.7.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-update-0.7.0.zip)

[ソース＋MOD一式ZIP 0.7.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UE-Minecraft-MVP-0.7.0.zip)

自分のスキン付き腕・全身、Minecraftの設定済みキーでの視点切り替え、バニラの音とブロック粒子。
[更新・書き出し・取り込み・テスト手順](../docs/UPGRADE_0.7.0.md)。MODとUE両方の更新・UE再ビルドが必要。
保存済みレベルを使用し、Sourceを削除せずZIP内のファイルを統合コピーしてください。
ビルド補助は必要なProceduralMeshComponentプラグインのみuprojectへ追加し、元ファイルをバックアップします。
今回はスキンと粒子のために素材を書き出し直します。配布物にユーザーのスキン・Minecraft音声/画像は含みません。
Java63件/Python21件成功。UE5.8のビルド・統合テストは実機で必要です。

## 過去版 0.6.0

**UEビルドには[修正パッチ0.6.1](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-build-fix-0.6.1.zip)も適用してください。** C4458のMesh/Owner名前衝突を修正。MOD0.6.0はそのまま。[適用手順](../docs/BUILD_FIX_0.6.1.md)。

[MOD 0.6.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.6.0.jar)

[UE更新ZIP 0.6.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-update-0.6.0.zip)

[ソース＋MOD一式ZIP 0.6.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UE-Minecraft-MVP-0.6.0.zip)

照準枠、UEでの設置・破壊、腕と手・持ち物、移動調整、非同期映像転送と遅延計測。
**MODとUEの両方を更新し、UEを再ビルド**してください。[更新・操作・テスト手順](../docs/UPGRADE_0.6.0.md)。
Sourceは使用中uprojectの横へ統合コピー。既存Content/Config/Savedを保持し、素材再インポートは不要。
Java47件/Python10件成功。UE5.8ビルド・GPU・描画・統合検証はPCで必要です。

[UE移動修正ZIP 0.5.2](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-movement-fix-0.5.2.zip)：初回着地後の移動停止とジャンプ速度を修正。ファイルを上書きしUE再ビルド。MOD0.5.0は維持。

## UEビルド修正 0.5.1

[UEビルド修正ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-build-fix-0.5.1.zip)

0.5.0でBridgeWorld.cppのC4458/C2064が出る場合に適用。Sourceを上書きして再ビルド。
MOD0.5.0と保存済みレベル/素材は変更不要です。下の0.5.0アーカイブには修正が入っていません。

## 過去版 0.5.0

[MOD 0.5.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.5.0.jar)

[UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-update-0.5.0.zip)

[ソース＋MOD一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UE-Minecraft-MVP-0.5.0.zip)

Minecraft入力/HUD、UE移動・衝突。初期地形転送後のUE地形をPlay中に保持。
[更新・起動・テスト手順](../docs/UPGRADE_0.5.0.md)。MODとUE両方の更新・UE再ビルドが必要。
Sourceフォルダも必ずコピーしてください。Content/Config/Savedや素材を置換しません。
素材取り込み修正0.4.3を含みます。既に素材が動いている場合は再インポート不要。
Java39件/Python10件成功、UE実機ビルド・移動/衝突確認は必要です。


## 過去版 0.4.0

[Minecraft MOD 0.4.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.4.0.jar)

[UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-update-0.4.0.zip)

[ソース＋MOD一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UE-Minecraft-MVP-0.4.0.zip)

ブロックテクスチャのローカル書き出し/UE取り込み、映像品質3段階、露出調整。バニラ素材は配布物に含めません。
**MODとUEを両方更新し、UEを閉じた状態で再ビルド**してください。
[導入・素材取り込み・テスト手順](../docs/UPGRADE_0.4.0.md)。既存のレベルを保持する更新パッチです。
0.3.1のPythonパス修正を含みます。Java/Pythonテスト成功。今回のUE実機ビルド・描画確認は必要です。


## マテリアル設定スクリプト修正版 0.3.1

[設定スクリプト修正ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-material-setup-fix-0.3.1.zip)

UEの`Paths.project_file_path`のAttributeErrorを修正。ZIP内の`setup_world_bridge.py`だけを使用中のUEBridgeへ上書きし、
Pythonモードで再実行してください。MOD 0.3.0とUE C++はそのままで、再ビルドは不要です。
下の0.3.0 ZIPは以前のスクリプトを含むため、この修正も適用してください。実UE実行の確認は必要です。

## 過去版 0.3.0

[Minecraft MOD 0.3.0のJAR](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.3.0.jar)

[UE側更新パッチ＋ビルド補助](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-update-0.3.0.zip)

[ソースとMODの一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UE-Minecraft-MVP-0.3.0.zip)

**今回はMODとUEの両方を更新します。** 周辺ワールド自動同期・UE映像のMinecraft HUD表示・しゃがみを追加。
[導入・テスト手順](../docs/UPGRADE_0.3.0.md)。UEパッチはSourceと補助スクリプトのみで、Content/Config/Savedを含みません。
MODビルドと通信テストをクラウドで確認。UEの実機ビルド・描画検証は必要です。
以下の0.2.xは過去版です。

## UEカメラ左右修正 0.2.2

[UE側ソース更新パッチ](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-camera-fix-0.2.2.zip) / [更新・低FPS切り分け手順](../docs/CAMERA_FIX_0.2.2.md)

MOD 0.2.1はそのまま使用します。UE Editorを閉じ、パッチのSourceを使用中のUEBridgeへコピーして再ビルドしてください。Content/Config/Savedは含まず、作成したレベル/アセットを置き換えません。UE実機ビルド・動作は未検証です。数FPSについては、先にエディタの背景CPU抑制をOFFにして確認してください。

## 起動修正版 0.2.1（過去版）

[修正版MODのJARをダウンロード](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.2.1.jar)

Minecraftを終了し、modsから旧Bridge MOD（0.1.0/0.2.0）だけを取り出し、このJARへ差し替えてください。Fabric APIは残します。Minecraft 1.21.11・Java 21・Fabric Loader/APIの変更、UEソースの更新は不要です。

[ソースとMODの一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UE-Minecraft-MVP-0.2.1.zip)

原因: Mixin専用パッケージが通常の初期化クラスまで含んでいたためFabricがクラス読み込みを拒否。Mixinだけを専用サブパッケージへ移動し、回帰テストを追加しました。ビルドとJava 17件のテスト成功。GUI起動はPCで再確認が必要です。

旧版アーカイブにはこの起動不具合があります。今後の導入には0.2.1を使ってください。

## 改良版 0.2.0

[UE-Minecraft-MVP-0.2.0.zip をダウンロード](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UE-Minecraft-MVP-0.2.0.zip)

ビルド済みFabric 1.21.11 MOD・UEソース・設定手順を含みます。
接続診断、TNT判定改善、周辺ブロックの手動プレビュー、任意の弓試作を追加。
[更新手順と未検証項目](../docs/UPGRADE_0.2.0.md)を確認し、MC MODとUEを両方更新してください。
UEのVFX/破壊壁アセットは未作成で、UE 5.8 C++ビルドと実機動作は未検証です。
ZIPのチェックサムは `UE-Minecraft-MVP-0.2.0.zip.sha256` にあります。

保存できない場合はZIPのGitHubページを開き、右上の **Download raw file**（下向き矢印）を押してください。
非公開リポジトリの場合はGitHubへログインします。

## 旧版 0.1.0（既存リンクは維持）

[UE-Minecraft-MVP-source-and-mod.zip](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UE-Minecraft-MVP-source-and-mod.zip)

最初に提供したZIPです。導入中のファイルは差し替えていません。
SHA-256: `24e07cf382477ca0281175322767ff1cbad98a2b609b402f0c4475c9ba9feed0`

どちらもMODを使うには対応するFabric LoaderとFabric APIが別途必要です。

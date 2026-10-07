# ダウンロード

## 更新版0.10.0

- [MOD 0.10.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.10.0/downloads/minecraft-ue-bridge-0.10.0.jar)
- [既存UE用更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.10.0/downloads/UEBridge-update-0.10.0.zip)
- [ソース＋MOD一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.10.0/downloads/UE-Minecraft-MVP-0.10.0.zip)
- [上書き対象・再書き出し・取り込み・操作順](../docs/UPGRADE_0.10.0.md)

色・アウトライン・腕の残像対策、主手のネイティブ持ち物モデル、地上モブのスポーンエッグ、開閉／スイッチ音、クリエイティブ限定の飛行。0.9.4までの修正を含み、旧パッチは不要です。
**MODとUE両方の更新・UE再ビルド・素材の再書き出し／取り込みが必要です。**
Sourceを削除せず統合コピーし、既存Content/Config/Saved/レベルを保持します。
Java108件・Python49件と独立C++計算が成功。UE5.8のビルド・描画・Windows統合動作は未確認です。
全アイテムの全状態や全モブ固有AIの完成ではありません。対応範囲は操作手順に記載しています。
新しい配布ブランチは `ue-bridge-0.10.0`。以前のZIPとmainは保持します。

## UE腕・持ち物マテリアル修正0.9.4

- [UE修正パッチZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/UEBridge-material-fix-0.9.4.zip)
- [1ファイルの上書き・再ビルドとIDLE時の操作](../docs/UE_MATERIAL_FIX_0.9.4.md)

BridgeCharacter.cppの動的マテリアル親の警告を修正します。
MODは0.9.3、UEの他のファイルと素材は保持します。UEビルド・描画はクラウドでは未確認です。

## MOD・取り込みスクリプト修正0.9.3

- [MOD 0.9.3](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/minecraft-ue-bridge-0.9.3.jar)
- [取り込みスクリプト更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/UEBridge-texture-import-fix-0.9.3.zip)
- [具体的な操作順](../docs/UPGRADE_0.9.3.md)

ブロック状態のJava列挙名とMinecraft保存用の名前の違いを修正。
MODとimport_minecraft_textures.pyを更新し、ブロック素材を再書き出し・取り込みします。
今回の修正だけならUE再ビルドは不要です。UEソースは0.9.0＋修正0.9.2を使用します。
MODビルド・Java97件・Python42件成功。UE取り込み・描画の実機確認は未実施です。

## 更新版0.9.0

**UEビルドには[累積修正パッチ0.9.2](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/UEBridge-build-fix-0.9.2.zip)も適用してください。**
UE5.8でのヘッダー順序・JSONキー変換・雲フラグ・Role変数のエラーを修正。
0.9.1の存在しないGetDataメンバー呼び出しも修正し、0.9.1の変更をすべて含みます。
MODは0.9.0のまま、UEの3ファイルを上書きして再ビルドします。[適用手順](../docs/BUILD_FIX_0.9.2.md)。

- [MOD 0.9.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/minecraft-ue-bridge-0.9.0.jar)
- [既存UE用更新ZIP 0.9.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/UEBridge-update-0.9.0.zip)
- [ソース＋MOD一式ZIP 0.9.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/UE-Minecraft-MVP-0.9.0.zip)

一人称のFOV70投影、破片調整、通常ブロックのモデル・当たり判定・設置、
地上モブの基本AI、照明オフと実際のMinecraftの空の合成。
[上書き対象・操作順・診断](../docs/UPGRADE_0.9.0.md) / [ブロック対象範囲](../docs/BLOCK_SUPPORT_0.9.0.md)。
モブ固有AI・飛行／水中・装備等の描画、全アイテム、全ブロック固有挙動は未完成です。

MODとUEを両方更新し、UEを閉じて再ビルドします。Sourceは削除せず統合コピーします。
ブロック・スキン・モブ素材をMC専用環境から再書き出し、UEに再取り込みしてください。
更新ZIPはContent/Config/Saved/uprojectを含まず、既存レベル・素材を保持します。
0.8.2の粒子設定修正を含むため、その旧パッチを重ねて適用する必要はありません。
配布ブランチは `ue-bridge-0.9.0`。旧版ZIP・mainは保持します。

クラウドでMODビルド、Java95件/Python41件、独立C++計算53項目とマスク15往復が成功。
実際の1.21.11ローカル素材1008静的モデルIDの解決・検証も成功しました。
**UE5.8のビルド・描画・Windows統合動作は未確認です。**

## 更新版 0.8.0

**粒子素材の設定には[修正パッチ0.8.2](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.8.0/downloads/UEBridge-particle-setup-fix-0.8.2.zip)も適用してください。**
UE 5.8のUV入力接続エラーと0.8.1の`default_value`プロパティエラーを修正します。[適用手順](../docs/PARTICLE_SETUP_FIX_0.8.2.md)。
setup_vanilla_effects.pyだけを更新します。MOD/C++は0.8.0のまま、再ビルドは不要です。

[MOD 0.8.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.8.0/downloads/minecraft-ue-bridge-0.8.0.jar)

[UE更新ZIP 0.8.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.8.0/downloads/UEBridge-update-0.8.0.zip)

[ソース＋MOD一式ZIP 0.8.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.8.0/downloads/UE-Minecraft-MVP-0.8.0.zip)

粒子診断・素材設定、しゃがみジャンプ、着地音、腕・持ち物・胴体、目線とダッシュFOVを改善。
設定済み前進キーの二度押しダッシュを追加。[導入と実機チェック](../docs/UPGRADE_0.8.0.md)。
配布ブランチは `ue-bridge-0.8.0`。従来のmainと旧版配布物は保持しています。
MODとUEを両方更新し、UEを閉じて再ビルドした後、粒子素材を再設定してください。
UE更新ZIPはContent/Config/Saved/uprojectを含みません。Sourceは削除せず、使用中プロジェクトへ統合コピーします。
0.7.1の修正を含むため、旧パッチの追加適用は不要です。UE5.8のビルド・描画はクラウドでは未検証です。
MODビルド、Java78件/Python27件、独立C++計算チェック35項目成功。粒子の実機不表示の原因は診断値で確認します。

## 過去版 0.7.0

**UEビルドには[修正パッチ0.7.1](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-build-fix-0.7.1.zip)も適用してください。**
粒子コードのC2668を修正し、GPU終了処理の旧APIを更新。MODは0.7.0のまま。[適用手順](../docs/BUILD_FIX_0.7.1.md)。

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

# Minecraft ↔ Unreal Engine bridge MVP

**描画・入力・戦闘修正0.15.2：** [UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.15.2.zip) / [MOD 0.15.2](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/minecraft-ue-bridge-0.15.2.jar) / [更新手順](docs/UPGRADE_0.15.2.md)。MOD交換・新しいnative export・UE再ビルド・再取り込みが必要です。Windowsの実描画・入力は未確認です。


Fabric **Minecraft Java 1.21.11 / Java 21** から地形・素材・スキン・HUD・操作設定・音を
書き出し、**Unreal Engine 5.8で直接プレイ** する実験用プロジェクトです。
書き出し後はMinecraftを終了できます。UE映像をMinecraftへ送る従来の接続モードも残しています。

**導入手順：** [Windows向け0.15.0導入ガイド](docs/INSTALL_0.15.0_JA.md)（初回導入・既存更新・native export・PowerShell・トラブル対処）

**バニラ参照変換・取り込み修正0.15.0：** Minecraft 1.21.11の実データと参照コードを使い、草ブロックの土面・バイオーム色・水、空と光、ItemGroups順・装備枠、攻撃音／無敵時間／ノックバック／死亡パーティクル、しゃがみ・飛行慣性・感度曲線、モブの20Hz制御を更新しました。元のMinecraftソースやバニラ素材は配布物へコピーしていません。[UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.15.0.zip) / [MOD 0.15.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/minecraft-ue-bridge-0.15.0.jar) / [一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UE-Minecraft-MVP-0.15.0.zip) / [更新手順](docs/UPGRADE_0.15.0.md) / [検証記録](docs/AUDIT_0.15.0.md)。全変更を反映するには0.15.0で新しいnative exportが必要です。今回のWindows UEコンパイル・描画・統合動作は未確認です。

**操作・描画・戦闘の修正0.14.0：** 報告された14項目の実装修正をまとめました。保存中の停止、感度、インベントリ、F5、飛行慣性、攻撃回復、赤い被ダメージ表示・受付間隔、死亡時の接地、歩行・ノックバック、羊毛・アイコン、照明ONの時刻反映に対応します。[UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.14.0/downloads/UEBridge-update-0.14.0.zip) / [MOD 0.14.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.14.0/downloads/minecraft-ue-bridge-0.14.0.jar) / [更新手順](docs/UPGRADE_0.14.0.md) / [検証記録](docs/AUDIT_0.14.0.md)。全項目を反映するには新しいnative exportが必要です。今回のWindows UEコンパイル・描画・統合動作は未確認です。

**取り込み完了後の終了判定修正0.13.2：** 利用者ログで素材・モブ・UI・音・専用マップの取り込み完了を確認しました。UEが終了ログを書いた後にアクセス違反コードを返した場合、今回の完了マーカー・マップ・ログを検証して保存済みマップを起動します。[UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.13.2/downloads/UEBridge-update-0.13.2.zip) / [保存済みマップの即時起動・更新手順](docs/UPGRADE_0.13.2.md)。描画・ゲーム操作と終了時アクセス違反の内部原因は未確認です。

**モブ取り込み修正0.13.1：** UE 5.8.3のログで確認した `Cannot nativize 'Quat' as 'Rotator'` を修正しました。0.13.0適用済みなら `import_minecraft_mobs.py` だけを差し替えて再試行できます。[UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.13.1/downloads/UEBridge-update-0.13.1.zip) / [更新手順・検証](docs/UPGRADE_0.13.1.md)。利用者ログではテクスチャ・アイテム段階を通過しています。修正後のUE実取り込み完了は未確認です。

**追加調査・原作への改善0.13.0：** アイテム／空の取り込み停止、保存失敗時のパレット復元、Python更新の自動再取り込み、オフハンドの交換・表示・保存、ボタン時間、矢の基本ダメージ、段差・到達距離・入力復帰・太陽／月を修正しました。[UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.13.0/downloads/UEBridge-update-0.13.0.zip) / [MOD 0.13.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.13.0/downloads/minecraft-ue-bridge-0.13.0.jar) / [一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.13.0/downloads/UE-Minecraft-MVP-0.13.0.zip) / [更新手順](docs/UPGRADE_0.13.0.md) / [調査・検証記録](docs/AUDIT_0.13.0.md)。旧MOD 0.12.0の書き出しは継続できます。1.21.11の太陽・月画像の不足には新MODでの再書き出しが必要です。

**Windows UE 5.8.3の0.12.1 C++ビルド成功と、nativeテクスチャ取り込みへの進行は利用者ログで確認済みです。0.13.0のUEコンパイル・実取り込み完了・描画は未確認です。** 0.12.1～0.12.4の停止修正は0.13.0に含まれます。旧版の配布物・ブランチも保持しています。

**UE単独プレイ0.12.0：** `/uebridge native export` の1回で書き出し、**Play-Native.cmd** の初回ファイル選択でビルド・取り込み・起動。次回はダブルクリックで保存状態を再開します。直接入力、手元のリソースパックを使うHUD、インベントリ・検索、モブPaletteの作成と割り当て、光・粒子・診断の修正、地形・所持品・モブ・投下物の永続化を実装しました。

[MOD 0.12.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.0/downloads/minecraft-ue-bridge-0.12.0.jar) / [既存UE用更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.0/downloads/UEBridge-update-0.12.0.zip) / [一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.0/downloads/UE-Minecraft-MVP-0.12.0.zip) / [最小操作の導入手順](docs/UPGRADE_0.12.0.md)。

**Minecraftの完全移植には未達です。** クラフト、食料・経験値・防具、流体、全モブ固有AI等は未実装です。地形はロード済みの有限範囲で、UEの編集はUEの保存へ記録します。UE 5.8のWindowsビルド・描画・IME・実FPSはクラウドでは未確認です。[対応範囲と実機確認](docs/NATIVE_PLAY.md)に、実装済みの機能と残る機能を記載しています。

## 過去版の更新履歴

**UEメモリ修正0.11.5：** セル更新時のProceduralMesh再利用と古いコンポーネント解放。[2ファイルの上書き・再ビルド手順](docs/MEMORY_FIX_0.11.5.md)。`control ue`中のページングファイル不足対策です。

**映像再接続修正0.11.6：** `UE映像待ち: EOFException` でGPU共有の接続が切れた場合、次の接続をJPEGへ自動降格するMODです。[JARの差し替え手順](docs/VIDEO_RECONNECT_FIX_0.11.6.md)。UEの再ビルド・素材再取り込みは不要です。

**UEビルド修正0.11.1：** 0.11.0を使う場合は[5ファイルの修正パッチと再ビルド手順](docs/BUILD_FIX_0.11.1.md)も適用してください。MODは0.11.0のままです。

**更新版0.11.0：** モブ召喚・初期取り込み後の追加転送、UE照明とバニラ風の光の分離、昼夜・光源・AO、アイテム投下と拾得、アトラス／隠れた面の削減、UE位置に追従する地形取得、WindowsのGPU共有経路と性能計測を追加。
**MODとUEを両方更新・UE再ビルド・素材再書き出し／取り込みが必要です。**
[MOD](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/minecraft-ue-bridge-0.11.0.jar) / [UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/UEBridge-update-0.11.0.zip) / [具体的な更新・計測手順](docs/UPGRADE_0.11.0.md)。
目標はRTX 5060で1080p・4～6チャンク・実映像30fps以上です。**UE5.8ビルド・Windows GPU共有・描画・目標FPS達成はクラウドでは未確認です。** D3D11の共有経路と通常D3D12のJPEG経路を実機で比較します。

**過去のMOD・アイテム取り込み修正0.10.1：** `Item manifest exceeds 64 MiB` を修正。全モデルJSONを圧縮保存し、新しい取り込みスクリプトで検証・展開します。[MODとPython1ファイルの更新手順](docs/ITEM_EXPORT_FIX_0.10.1.md)。UEは0.10.0のまま、C++再ビルド不要です。

**更新版0.10.0：** 映像の色変換・照明OFF時の白浮き対策、黒いネイティブ形状のアウトライン、腕の振り時間・ボブ・残像対策、持ち物のネイティブモデル取得、地上モブのスポーンエッグ、開閉／スイッチ音、クリエイティブ限定の飛行を実装。
**MODとUEを両方更新・UE再ビルド・素材再書き出し／取り込みが必要です。**
[ダウンロード](downloads/README.md) / [使用中プロジェクトへの具体的な更新手順](docs/UPGRADE_0.10.0.md)。0.9.4までの修正を含みます。UE5.8ビルド・描画はクラウドでは未確認です。
主手の静的モデルが対象です。オフハンド、光沢、特殊描画アイテム、モブ固有AI等の範囲は更新手順に記載しています。

**過去のUE修正0.9.4：** 腕・持ち物の動的マテリアルを親にする警告を修正。
[1ファイルの更新・再ビルドと初期転送の開始条件](docs/UE_MATERIAL_FIX_0.9.4.md)。MODは0.9.3を使用します。

**過去の修正0.9.3：** ブロック状態の書き出し名をMinecraftの保存用の名前へ修正。
`Invalid state property` が出る場合は[MODと取り込みスクリプトの更新手順](docs/UPGRADE_0.9.3.md)を使い、素材を再書き出ししてください。この修正だけのためにUE再ビルドは不要です。

**更新版0.9.0：** 一人称の固定FOV70投影、破片の調整、通常ブロックのモデル／衝突／設置、
地上モブの基本AI、照明オフとMinecraftの実際の空の合成を追加。
**0.9.0の配布ZIPには[UEビルド修正パッチ0.9.2](docs/BUILD_FIX_0.9.2.md)も適用してください。** 0.9.1の修正も含みます。MODは0.9.0のまま、UEの3ファイルを上書きして再ビルドします。
**MODとUEを両方更新し、素材も再書き出し・取り込み**してください。
[ダウンロード](downloads/README.md) / [具体的な更新操作](docs/UPGRADE_0.9.0.md) / [ブロック対象範囲](docs/BLOCK_SUPPORT_0.9.0.md)。
モブ固有AI・飛行／水中・追加描画層、全アイテム、全ブロック固有挙動は未実装です。
UE5.8ビルド・描画はWindows実機での確認が必要です。

**過去版0.8.0：** 粒子の準備・生成・表示診断、しゃがみジャンプ、クリエイティブの着地音、
腕／持ち物／胴体と床基準の目線、ダッシュFOV、設定済み前進キーの二度押しダッシュを追加・改善。
**MODとUEを両方更新**してください。[ダウンロード](downloads/README.md) / [導入とテスト](docs/UPGRADE_0.8.0.md)。
0.7.1までのビルド修正を含みます。保存済みレベル・素材を保持し、粒子マテリアルを再設定します。
0.8.0の配布ZIPには[粒子素材設定の修正パッチ0.8.2](docs/PARTICLE_SETUP_FIX_0.8.2.md)も適用してください。0.8.1の修正を含み、MOD/C++の再ビルドは不要です。
スキン/粒子の素材は専用Minecraft環境からローカルに取り込み、配布物には含めません。

## 現在の状態

- 0.12.0ではMinecraftを素材・地形の書き出しに使い、UEで直接入力・HUD描画・プレイ・保存します。Minecraftへの映像配信を省くため、その経路の圧縮・転送待ちは発生しません。UE自体の実FPSは実機で確認します。
- 1回の書き出しパッケージのハッシュ・素材・Paletteを検証し、専用nativeマップを作成します。既存UEレベルとMinecraftの元ワールドは保持します。
- 地形・プレイヤー・インベントリ・モブ・投下物・着火済みTNTを同じUE保存ファイルへ記録します。書き出しは水平4～6チャンク・上下104ブロックの有限範囲です。
- MODビルド、Java185件・Python123件と独立C++計算の検証が成功。PowerShellの構文とプラグイン設定保持も確認しました。UEモジュールビルド、実エディター取り込み、実描画、Windows入力・IME・性能は未確認です。[対応範囲と実機確認](docs/NATIVE_PLAY.md)を参照してください。

### 過去版の確認記録

- 0.10.0のMODビルド、Java108件・Python49件、独立C++計算53項目・アウトライン8項目・マスク15往復成功。UEモジュールビルド・実機描画は未確認。

- 0.9.0のMODビルド・Java95件・Python41件、UEから独立したC++計算53項目とマスク15往復が成功。失敗・スキップ0。
- 0.9.3のMODビルド・Java97件・Python42件成功。ネイティブの14プロパティ型・58値の保存名を検証し、旧方式と異なる7値を確認しました。
- 実際の1.21.11ローカル素材1168ブロック状態ファイルを検査。1008の静的モデルID・2055モデル・1075テクスチャを解決し素材検証が成功。専用／不可視160IDを除外。これはUEでの全状態描画や全ゲーム挙動の確認ではありません。
- 0.7.0＋UE修正0.7.1のスキン付き腕・全身、視点切り替え、移動・設置・破壊はユーザー実機で確認済み。
- 0.8.2の粒子素材設定と表示はユーザー実機で成功。
- **0.9.0のUE5.8ビルド・描画・統合動作は実機確認が必要**。クラウドにUE Editorはありません。
- ユーザー実機の0.9.0と修正パッチ0.9.1のビルドログをもとに、累積修正パッチ0.9.2を公開。修正後のUEビルド・描画は未確認です。
- 保存済みレベルを継続使用。今回の粒子マテリアルは補助スクリプトで再設定します。
- 腕と全身は自分のスキン。主手の持ち物はローカルに取得したネイティブモデルを使います。取得できないモデルは診断へ表示します。
- UE地形の直接編集、地上モブの卵生成、クリエイティブ限定の飛行。サバイバルの全ルールは未実装。MCワールドは変更せず、UEの変更は同じPlay中に保持。
- 映像は最大1080p/60fpsの設定に対応。実際のfps・遅延はPCで測定。JPEG方式を比較・互換用に残し、Windows D3D11/OpenGLのGPU共有経路も実装。空合成には同じHDR撮影の透明度を使います。共有の実機動作・目標FPSは未確認です。

## 保存先

```text
minecraft-mod/           Fabric MOD（Gradle Wrapper同梱）
unreal/UEBridge/         新規C++ UEプロジェクト
bridge/PROTOCOL.md      独立した通信仕様
bridge/smoke.py         MOD受信確認 / UE向けテスト送信
docs/UE_SETUP.md        カメラ・Niagara・Chaosのエディタ設定
docs/TESTING.md         起動と最初の成功条件の検証
tools/build_mod.py      クラウド用プロキシ対応ビルド
tools/setup_world_bridge.py  保存済みテストレベルの色付き地形設定
tools/import_minecraft_textures.py  ローカル素材のUE取り込み
tools/import_minecraft_player.py    スキン/最新の素材をまとめて取り込み
tools/import_minecraft_items.py     主手のネイティブ持ち物モデルを取り込み
tools/import_minecraft_mobs.py      地上モブ・スポーンエッグ用素材を取り込み
tools/import_minecraft_atlas.py    地形の描画用アトラスを作成
tools/bridge_lighting_materials.py UE照明／バニラ風光の生成素材
tools/setup_bridge_rendering.py    生成素材の照明分離・黒い輪郭を設定
tools/setup_vanilla_effects.py      UE内のバニラ風ブロック粒子設定
unreal/UEBridge/Build-UEBridge.cmd  Windows用C++ビルド補助
tools/import_native_play.py        同じnative書き出しを検証・一括取り込み
tools/import_minecraft_ui.py       HUD・アイコン・フォントを取り込み
tools/import_minecraft_sounds.py   ローカルで書き出した音を取り込み
unreal/UEBridge/Play-Native.cmd     UE単独プレイの初回準備・起動・再開
```

GitHubはファイルの保存場所です。Minecraft/UEそのものをGitHub内で起動するわけでは
ありません。PCにはリポジトリをcloneまたはZIPでダウンロードします。例えば
`Documents/UE-MINECRAFT` が保存先として使えます。他のプロジェクトと分けてください。

## MODビルドと起動

Java **JDK 21**（JREのみでは不可）をインストールし、`JAVA_HOME` をその場所に設定。
Gradleの別途インストールは不要です。

Windows PowerShell:

```powershell
cd minecraft-mod
.\gradlew.bat build
```

macOS/Linux:

```sh
cd minecraft-mod
./gradlew build
```

`build/libs/minecraft-ue-bridge-0.12.0.jar` がMOD本体です（`-sources.jar`ではありません）。
Minecraft Launcherに **1.21.11 / Fabric Loader 0.19.5** の専用インストールを作り、
ゲームディレクトリを新しい `MC-UE-Test` フォルダに設定してください。その `mods/` に
本MODと **Fabric API 0.141.6+1.21.11** を配置します。新しいシングルプレイ・クリエイティブ
ワールドを作成します。既存のMinecraftディレクトリ、ワールド、サーバーは使いません。
Minecraftの購入済みアカウントによる起動認証は通常のLauncherで行ってください。

開発者向け起動は `gradlew runClient` ですが、受け入れテストはLauncherの専用環境で行います。

## UE単独プレイの起動

導入済みの専用Minecraftで `/uebridge native export` を実行し、完了後にMinecraftを終了します。
`UEBridge.uproject` の隣の **Play-Native.cmd** をダブルクリックし、初回だけ書き出された
`native_manifest.json` を選びます。必要なビルドと一括取り込みの後、専用レベルでUEゲームが起動します。
次回は同じCMDをダブルクリックして保存状態を再開します。
[具体的な更新・導入操作](docs/UPGRADE_0.12.0.md)を参照してください。

## 従来のUE↔MC接続モード

UE 5.8と対応C++ビルドツールが必要です。WindowsではEpicの対応表に合う
Visual Studio 2022の「C++によるゲーム開発」とWindows SDKを用意してください。
UE 5.8のインストール先・実機対応はこのクラウドから確認していません。

1. `unreal/UEBridge/UEBridge.uproject` をUE 5.8で開き、C++モジュールをビルド。
2. [UE_SETUP.md](docs/UE_SETUP.md) に従って新規レベル・Niagara・破壊壁を作成。
3. UEの **Play（Selected Viewport / 1 Player）** を開始。
4. Minecraftの専用ワールドに入り、視点を動かす。
5. TNTを壁の近くに対応する位置へ置き、火打石で着火。

UE側は **着火直後** に爆発します。Minecraft側の導火線完了とは時間が異なります。
TNTを置くだけでは発火しません。レッドストーン・連鎖爆発はMVP対象外です。

詳しいチェックと障害切り分けは [TESTING.md](docs/TESTING.md)。

**UE主体の移動・衝突の更新手順は [UPGRADE_0.5.0.md](docs/UPGRADE_0.5.0.md)。**
旧版ダウンロードZIPは保持しています。最新版のMODとUEを両方揃えてください。

## 従来の接続モードの設計上の範囲

- localhost UDP `127.0.0.1:7779`。カメラ入力は最大120Hz（実FPS以下）。
- 映像は独立したlocalhost TCP `127.0.0.1:7780`。既定960×540・最大60fps JPEG（実FPSはPC性能で変化）。圧縮/デコードは別スレッド。
- 1ブロック = UE 100cm。MC `(x,y,z)` → UE `(z,-x,y)`、Yawはそのまま、Pitchは符号反転。
- 従来モードはMC位置をUEへコピー。新モードはMC移動を固定し、キーをUE CharacterMovementへ渡す。
  初期地形の衝突と移動・重力・ジャンプはUEが計算する。
- 移動キー・ジャンプ・しゃがみ状態をBlueprintに公開。実際の体高と目線を同期する。
- TNT・弓・ワールドイベントは再送/ACK・重複排除。通常の視点入力にはACK待ちなし。
- 接続の基準位置はMCワールド入場時とUE PlayerStart。両方再起動すると基準を揃え直せます。
- 壁破壊は `BridgeWall` タグ付きGeometry Collectionのみ。床や既存シーン全体を対象にしません。
- 自動地形同期は8ブロック単位。新モードは初期転送完了後にUEの地形を保護し、MCから上書きしない。
  0.11.0はUE位置に追従する範囲と同じセッションの編集保持に対応。地上モブの基本動作とUE体力を実装し、リアル水・モブ固有AI・ディスク永続化は未実装。
- 視点はMinecraftの実際の設定値を送信。F5固定ではなく、マウスボタンに割り当てた切り替えも使用可能。
- 音はUEの結果をACK/再送してMinecraftのSoundManagerで再生。既存のリソースパックの音を使用。
- 粒子はローカルに取り込んだブロックのparticleテクスチャでUE内に描画。バニラの20Hzの粒子値を使い、照明と衝突はUEが計算。

## クラウドでの再ビルド

```sh
python3 tools/build_mod.py build
python3 -m unittest discover -s bridge/tests -v
python3 tools/test_character_math.py
```

プロキシとシステムのJava CAストアを利用し、TLS検証を維持します。JDKとGradleの
配布チェックサムを確認して導入しました。ビルド成功はゲーム実機テストの代替ではありません。
C++チェックは製品コードと同じ計算ヘッダーをC++17で検証し、UEモジュールやCharacterMovementは実行しません。

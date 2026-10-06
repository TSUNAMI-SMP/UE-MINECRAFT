# Minecraft ↔ Unreal Engine bridge MVP

Fabric **Minecraft Java 1.21.11 / Java 21** と **Unreal Engine 5.8** を同じPCで
接続する実験用プロジェクト。Minecraftが操作・プレイヤー移動を担当し、UEが描画・物理を担当します。新モードではUEがプレイヤーの移動・衝突も計算します。既存サーバーへのインストールは不要です。

**最新版0.6.0：** UEでの照準アウトライン・ブロック設置/破壊、腕と手、持ち物表示を追加。
移動を調整し、非同期GPU読戻し・画素一括コピー・全画面時のMC描画省略で映像転送を改善。
**MODとUEを両方更新**してください。[ダウンロード](downloads/README.md) / [導入とテスト](docs/UPGRADE_0.6.0.md)。
UEビルドのC4458を直す[修正パッチ0.6.1](docs/BUILD_FIX_0.6.1.md)も適用してください。MODは0.6.0のままです。

## 現在の状態

- MOD：Java21ビルド、JUnit47件・Python10件成功。失敗・スキップ0。
- 以前のカメラ・ワールド/映像・テクスチャ・UE主体の移動はユーザー実機で確認済み。
- **0.6.0のUE5.8ビルド・腕/枠・GPU読み戻し・統合動作は未検証**。クラウドにUE Editorはありません。
- 既存テストレベル・736種の素材を継続使用。素材の再インポートや腕のBlueprint配置は不要。
- 腕は標準のブロック状モデル。立方体の持ち物には素材を適用し、それ以外は簡易表示。
- クリエイティブでUE地形を直接編集。MCワールドは変更せず、UEの変更は同じPlay中に保持。
- 映像は最大1080p/60fpsの設定に対応。実際のfps・遅延はPCで測定。JPEG方式を継続し、GPU共有は今後の改善。

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
unreal/UEBridge/Build-UEBridge.cmd  Windows用C++ビルド補助
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

`build/libs/minecraft-ue-bridge-0.6.0.jar` がMOD本体です（`-sources.jar`ではありません）。
Minecraft Launcherに **1.21.11 / Fabric Loader 0.19.5** の専用インストールを作り、
ゲームディレクトリを新しい `MC-UE-Test` フォルダに設定してください。その `mods/` に
本MODと **Fabric API 0.141.6+1.21.11** を配置します。新しいシングルプレイ・クリエイティブ
ワールドを作成します。既存のMinecraftディレクトリ、ワールド、サーバーは使いません。
Minecraftの購入済みアカウントによる起動認証は通常のLauncherで行ってください。

開発者向け起動は `gradlew runClient` ですが、受け入れテストはLauncherの専用環境で行います。

## UEの起動

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

## 設計上の範囲

- localhost UDP `127.0.0.1:7779`。カメラ入力は最大120Hz（実FPS以下）。
- 映像は独立したlocalhost TCP `127.0.0.1:7780`。既定960×540・最大20fps JPEG（品質変更可能）。圧縮/デコードは別スレッド。
- 1ブロック = UE 100cm。MC `(x,y,z)` → UE `(z,-x,y)`、Yawはそのまま、Pitchは符号反転。
- 従来モードはMC位置をUEへコピー。新モードはMC移動を固定し、キーをUE CharacterMovementへ渡す。
  初期地形の衝突と移動・重力・ジャンプはUEが計算する。
- 移動キー・ジャンプ・しゃがみ状態をBlueprintに公開。実際の体高と目線を同期する。
- TNT・弓・ワールドイベントは再送/ACK・重複排除。通常の視点入力にはACK待ちなし。
- 接続の基準位置はMCワールド入場時とUE PlayerStart。両方再起動すると基準を揃え直せます。
- 壁破壊は `BridgeWall` タグ付きGeometry Collectionのみ。床や既存シーン全体を対象にしません。
- 自動地形同期は8ブロック単位。新モードは初期転送完了後にUEの地形を保護し、MCから上書きしない。
  固定範囲/Play中の保持まで。腕・水・Mob・HP・ディスク永続化は未実装。

## クラウドでの再ビルド

```sh
python3 tools/build_mod.py build
python3 -m unittest discover -s bridge/tests -v
```

プロキシとシステムのJava CAストアを利用し、TLS検証を維持します。JDKとGradleの
配布チェックサムを確認して導入しました。ビルド成功はゲーム実機テストの代替ではありません。

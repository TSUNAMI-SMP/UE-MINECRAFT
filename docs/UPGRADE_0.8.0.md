# 0.8.0：粒子診断、移動・身体表示・カメラの改善

Minecraft Java 1.21.11 / Fabric / Java 21、Unreal Engine 5.8 / Windows。
MODとUEの両方を更新します。0.7.1のビルド修正を含み、旧パッチの追加適用は不要です。
この版のUE C++ビルド・描画はクラウドでは未検証です。以下の実機チェックで確認してください。
MODビルド・Java78件・Python27件が成功し、独立したC++計算チェック35項目も成功しました。
追加のUE Automationテストはクラウドでは未実行です。以前の粒子不表示の実機原因はまだ確定していません。

## 1. ダウンロードして、使用中のプロジェクトへ更新

- [MOD 0.8.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.8.0/downloads/minecraft-ue-bridge-0.8.0.jar)
- [UE更新ZIP 0.8.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.8.0/downloads/UEBridge-update-0.8.0.zip)
- [ソース＋MOD一式ZIP 0.8.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.8.0/downloads/UE-Minecraft-MVP-0.8.0.zip)

今回の配布は `ue-bridge-0.8.0` ブランチです。従来のmainと過去のダウンロードは保持しています。
使用中のプロジェクトには「UE更新ZIP」を使います。一式ZIP内のConfigを使用中プロジェクトへ上書きしないでください。

1. UEで保存済みレベルを保存し、Minecraft、UE、Visual Studioを終了します。
2. 念のため、使用中の次のUEBridgeフォルダー全体を別の場所へコピーしてバックアップします。

   ```text
   C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/
   ```

3. `C:/UEBridgeTest/MC-Test/mods/` から古い `minecraft-ue-bridge-*.jar` だけをmodsの外へ移し、
   `minecraft-ue-bridge-0.8.0.jar` を入れます。Fabric APIと他のMODは残します。
4. UE更新ZIPを別の場所へ展開し、中の `Source` と補助ファイルを、上記UEBridgeフォルダーへ
   **統合コピー**します。同名ファイルは上書きします。`Source` 全体を先に削除しないでください。
5. コピー先で以下の並びを確認します。`Source/Source/` という二重フォルダーにしないでください。

   ```text
   UEBridge/
     UEBridge.uproject             ← 既存ファイルを使用
     Build-UEBridge.cmd
     Build-UEBridge.ps1
     import_minecraft_textures.py
     import_minecraft_player.py
     setup_vanilla_effects.py
     UPDATE_INSTRUCTIONS.md
     Source/UEBridge/BridgeCharacter.cpp
     Source/UEBridge/BridgeVanillaEffects.cpp
     Content/                     ← 保存済みレベル・取り込み済み素材を保持
     Config/                      ← 既存設定を保持
     Saved/                       ← 既存ファイルを保持
   ```

6. UEを閉じたまま、同じフォルダーの `Build-UEBridge.cmd` を実行します。
   必要なら `C:\Program Files\Epic Games\UE_5.8` を入力します。
   **BUILD SUCCESSFUL** を確認してから、同じ `UEBridge.uproject` を開きます。
   ビルドに失敗した場合は、最後の要約だけでなく最初の `error` 行を確認してください。

UE更新ZIPはContent/Config/Savedとuprojectを含みません。必要なエンジン付属プラグインは
ビルド補助が既存設定を保持して追加します。既存のNiagara・Chaos設定やレベルは継続使用します。

## 2. 粒子素材を更新

0.7.0で取り込んだテクスチャ・スキンはそのまま利用できますが、今回の粒子マテリアルは再設定します。
専用Minecraft環境で素材やスキンを書き出していない場合、またはリソースパック／スキンを変えた場合は、先に実行します。

```text
/uebridge textures export
/uebridge player export
```

書き出し完了まで待ちます。Minecraftの画像・音声・個人スキンはローカル専用で、配布物には含みません。

UEでいつもの保存済みレベルを開き、**Playを停止してすべて保存**します。
BridgeReceiverが1個だけあることを確認します。
出力ログの入力欄をPythonにし、次の2行を一行ずつ実行します。Windowsパスは `/` を使います。

```python
exec(open("C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/import_minecraft_player.py", encoding="utf-8").read())
setup_minecraft_visuals("C:/UEBridgeTest/MC-Test")
```

最新の完了済み書き出しを選んで取り込み、粒子素材を再生成し、BridgeReceiverへ割り当ててレベルを保存します。
最後の `Minecraft visuals ready` を確認します。途中でErrorがあった場合は未完了です。

## 3. 起動・操作

1. Minecraftの「設定 → ビデオ設定」で視野角を **80** にします。ユーザーの設定をMODが自動上書きすることはありません。
2. UEでPlayを開始し、Minecraftの専用クリエイティブワールドに入ります。
3. 地面に立って飛行を停止し、`/uebridge world off` → `/uebridge import start` を実行します。
4. `/uebridge import` で **READY** を確認してから `/uebridge control ue` を実行します。
5. `/uebridge status` でUEの版と粒子診断を確認します。

ダッシュは設定済みダッシュキー＋前進、または設定済み前進キーの二度押しを使います。
W固定ではありません。停止・後退・しゃがみ・メニュー表示・接続切れでダッシュを解除します。
ダッシュの開始・終了でFOVが滑らかに変わります。
視点切り替えは現在のMinecraft設定を使うため、マウスサイドボタンへの割り当ても継続できます。

## 4. 実機チェック

- **粒子**：石・木・草を破壊し、破片が出るか確認します。通常歩行では出ず、ダッシュ時は足元に出るか確認します。
  見えない場合は、破壊直後／ダッシュ中の `/uebridge status` とUEのOutput Logにある粒子診断を記録します。
  素材が準備済みでも表示成功を意味しません。要求・生成・表示数と抑制理由を比較します。
- **しゃがみジャンプ**：しゃがみキーを押したまま跳び、低い天井へ突き抜けないこと、着地後もしゃがみが保たれることを確認します。
- **着地音**：通常のジャンプと段差からの着地を、クリエイティブでも確認します。
  足元ブロックの既存音を使用し、ダメージ音は追加しません。同じ着地で音が重複しないことを確認します。
- **腕・持ち物**：空の手、石ブロック、持ち替え、攻撃を一人称と三人称で確認します。
  左利きとスリムスキンでも確認します。剣などの非ブロックアイテムは引き続き簡易表示です。
  バニラの基本表示に合わせ、一人称では空の手のときに腕を表示し、通常の持ち物があるときは持ち物を表示します。
  バニラは腕・持ち物を世界とは独立したFOV70で描画しますが、この版のUE映像では世界と同じ投影を使います。
  基本の変換とモデルサイズを合わせても、FOVやダッシュによる見かけのサイズには近似が残ります。
- **胴体**：三人称で横移動・後退・その場で視線変更を行い、胴体が滑らかに向きを変え、頭が視線へ追従することを確認します。
  見た目の向きによって移動方向や設置・破壊の照準が変わらないことも確認します。
- **カメラ**：同じ平面でバニラとUEの目線を比較し、立位／しゃがみの高さとFOV80を確認します。
  ダッシュ開始・停止でFOVが急に跳ねないこと、壁に押し付けたまま停止したときの見え方も確認します。
- **既存機能**：3視点、カメラの壁衝突、設置・破壊、音、`/uebridge control off` による復帰を確認します。
  Minecraft側のワールドがUE編集で変更されないことを確認します。

この版にMinecraftの空との合成、GPU共有、全ブロック／アイテムのゲーム動作、モブは含みません。
JPEG＋TCP映像は継続し、1080p設定だけで低遅延化済みとは扱いません。

## 粒子診断の読み方

`要求` と `生成` はUE Play中の累計、`最終` は最後の要求数→生成数です。
破壊粒子は短時間で消えるため、破壊後に現在数が0でも失敗とは限りません。
`登録` と `最大登録` はCPU側のインスタンス数で、画面に見えた粒子数ではありません。

| 理由 | 確認すること |
|---|---|
| `ready` | 基本の素材・画像・カメラ準備済み。表示成功は実機で確認します。 |
| `missing_material` / `material_parameters` / `material_instancing` | 新しい補助ファイルをコピーし、Play停止・保存後に素材取り込みの2行を再実行します。 |
| `missing_palette` / `missing_texture` | 専用ゲームフォルダーから素材を書き出し直し、取り込みます。最終の対象ブロックも確認します。 |
| `missing_surface` | UE地形の足元判定が取れていません。READYとUE操作状態、地形の衝突を確認します。 |
| `missing_camera` / `missing_plane` / `missing_effects` | BridgeGameMode・BridgeReceiverが1個であること、0.8.0のビルドと起動を確認します。 |
| `particle_limit` / `group_limit` | 粒子の負荷上限に達しています。少し待ち、単独の石ブロックで再確認します。 |
| `component_registration` / `material_instance` / `render_group_missing` / `render_submission` | UEのOutput Logも保存します。画像不足とは異なる生成・登録の失敗です。 |

生成と最大登録が増えているのに見えない場合は、マテリアルのコンパイル・照明・映像キャプチャなど描画側の切り分けが必要です。
ログと診断値を確認してから修正します。

## 開発者向けクラウド検証

リポジトリのルートで実行します。

```sh
python3 tools/build_mod.py build
python3 -m unittest discover -s bridge/tests -v
python3 tools/test_character_math.py
```

最後のコマンドはC++17コンパイラを使い、UEから独立した実際の身体表示・FOV計算を検証します。
UEモジュール、CharacterMovementの衝突・ジャンプ、シェーダーや実機描画の成功を意味しません。

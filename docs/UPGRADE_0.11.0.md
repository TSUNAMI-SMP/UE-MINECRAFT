# 0.11.0：モブ復旧・バニラ風の光・アイテム投下・1080pと4～6チャンク

**UEビルド前に[修正パッチ0.11.1](BUILD_FIX_0.11.1.md)の5ファイルも適用してください。** 元の0.11.0 ZIPは修正前のため、この追加パッチが必要です。MODは0.11.0を使用します。

この版はMOD・UEの両方を更新し、UEを再ビルドして、ローカル素材を再書き出し／取り込みします。0.10.1の圧縮アイテム書き出し修正も含みます。既存の配布物は保持します。

目標は **RTX 5060、1920×1080、描画距離4～6チャンク、実映像30fps以上** です。地形と転送の改善、計測、WindowsのGPU共有経路を実装しました。**クラウドにはUE5.8とWindowsのゲーム画面がないため、UEビルド・描画・GPU相互運用・実FPSの目標達成を確認済みとは扱いません。** 実機で測る手順を下に記載します。

## ダウンロード

- [MOD 0.11.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/minecraft-ue-bridge-0.11.0.jar)
- [使用中UEプロジェクト用の更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/UEBridge-update-0.11.0.zip)
- [ソース＋MOD一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/UE-Minecraft-MVP-0.11.0.zip)（新規導入用）

使用中プロジェクトには **UE更新ZIP** を使います。一式ZIPでプロジェクト全体を置き換えません。配布ブランチは `ue-bridge-0.11.0` です。

## 1. 終了・バックアップ・統合コピー

1. MinecraftとUEを終了します。使用中のUEBridgeフォルダーを別の場所へコピーしてバックアップします。
2. `C:/UEBridgeTest/MC-Test/mods/` の旧 `minecraft-ue-bridge-*.jar` を別フォルダーへ移し、新しい0.11.0のJARを入れます。本MODのJARは1個だけです。Fabric APIなどはそのままです。
3. UE更新ZIPを一時フォルダーへ展開します。中の `Source` フォルダーを、次の使用中フォルダーへ **統合コピー** し、同名ファイルを上書きします。Source全体は削除しません。

   ```text
   C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/
   ```

4. ZIP内の `Build-UEBridge.cmd`、`Build-UEBridge.ps1`、`Launch-UEBridge-GPU.cmd`、`Launch-UEBridge-GPU.ps1` と、次のPythonファイルも、**UEBridge.uprojectと同じフォルダー**へコピーします。

   ```text
   setup_world_bridge.py
   import_minecraft_textures.py
   import_minecraft_player.py
   import_minecraft_items.py
   import_minecraft_mobs.py
   import_minecraft_atlas.py
   bridge_lighting_materials.py
   setup_vanilla_effects.py
   setup_bridge_rendering.py
   ```

**Content／Config／Saved／UEBridge.uproject、保存済みレベル、取り込み済み素材を削除・置換しません。** 更新ZIPはこれらを含みません。フォルダー名の0.2.0は変更不要です。他のMinecraft環境・サーバー・UEプロジェクトには適用しません。

## 2. UEをビルドして、保存済みレベルを確認

UEを閉じた状態で使用中フォルダーの `Build-UEBridge.cmd` を実行します。インストールフォルダーを聞かれたら、実際の場所、例えば `C:\Program Files\Epic Games\UE_5.8` を入力します。ビルド成功を確認してから、いつもの `UEBridge.uproject` を開きます。

いつもの保存済みレベル `/Game/UE` を開き、Playを停止して保存します。World Outlinerに **BridgeReceiverがちょうど1個** 必要です。レベルやReceiverを追加して重ねる必要はありません。

保存済みレベルの床と取り込むMinecraftの床が重なる場合は、足元と照準を対応するMinecraftブロックへ解決する補正を入れました。粒子の拒否理由に `non_bridge_floor; level collider=...` が出たら、その名前のUE床を確認します。既存のUE床の衝突が掘った穴を塞ぐ場合は、Playを停止し、その床のCollision PresetsをNoCollisionへ変更してレベルを保存してください。必要ならその床の表示も停止します。スクリプトが保存済みレベルの床を自動削除することはありません。

## 3. Minecraftで素材を再書き出し

更新した専用Minecraftのシングルプレイに入ります。UE操作中なら `/uebridge control minecraft` に戻します。使用中のリソースパックを有効にし、表示したい持ち物をホットバーへ入れます。

次を1個ずつ実行し、毎回完了メッセージを待ちます。

```text
/uebridge textures export
/uebridge player export
/uebridge items export
/uebridge mobs export
```

- ブロックを書き直す理由：新しい光源の強さ、遮光、ネイティブの面の遮蔽情報を取得します。
- アイテムを書き直す理由：一／三人称のモデルに加え、投下時の **地上表示** を取得します。0.10.1の圧縮保存を継続し、64MiB上限での失敗を回避します。
- モブを書き直す理由：ゾンビ・村人を含む対応地上種のテンプレートと、近くの個体の外見を照合します。標準テンプレートの作成でMCワールドへモブを追加しません。

モブを何回かに分けて書き出しても構いません。一括取り込みは最新の完了したmanifestを使います。個体固有の外見は、その回に近くにいた個体が対象です。必要なゾンビ・村人がいる状態で最後の書き出しを行うと照合を確認しやすくなります。

書き出し先は `C:/UEBridgeTest/MC-Test/uebridge-export/` です。画像・音・個人スキンはGitHub配布物へ入れません。音はMinecraftが使用中のリソースパックから再生します。

## 4. UEへ取り込んで、レベルを保存

Playを停止し、レベルを保存してから、UE出力ログを **Python入力** に切り替えて実行します。Windowsパスにも `/` を使います。

```python
exec(open("C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/import_minecraft_player.py", encoding="utf-8").read())
setup_minecraft_visuals("C:/UEBridgeTest/MC-Test")
```

最新のブロック、スキン、アイテム、モブを取り込み、地形のテクスチャを描画用のアトラスへまとめます。生成済みのBridge用マテリアルだけを移行し、UE照明用とバニラ風の光用の出力を分けます。黒いアウトライン、粒子、共通の光パラメーターも設定して保存します。ユーザーが作ったレベルや無関係な素材は置き換えません。

粒子設定や生成素材の移行だけをやり直す場合は、Playを停止・レベル保存後に次を使えます。

```python
exec(open("C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/setup_vanilla_effects.py", encoding="utf-8").read())
setup_vanilla_effects()
exec(open("C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/setup_bridge_rendering.py", encoding="utf-8").read())
setup_bridge_rendering()
```

Tracebackが出たら、その全体を添付してください。素材検証の成功だけでは、Receiverへの割り当てや召喚成功まで確認できません。

## 5. まず通常のUE Playで確認

1. UEでPlayを開始します。
2. MCは飛行を止め、地面に立ち、しゃがまず次を実行します。

   ```text
   /uebridge performance target 4
   /uebridge video quality ultra
   /uebridge video fps 30
   /uebridge video transport auto
   /uebridge import start
   ```

3. `/uebridge import` または `/uebridge status` で初期地形が **READY** になるまで待ちます。
4. `/uebridge control ue` を実行します。視点切り替え、ジャンプ、しゃがみ、投下はMinecraftで設定しているキーを使います。F5、Space、Shift、Qに固定しません。

初回はMCのチャンク取得、転送、UEの描画・近傍衝突の構築が必要で、ワールドとPCによって時間がかかります。新しい地形・足元の衝突が準備中のときは、未読み込みの床へ進んで落下しないよう移動を待ちます。READY後の遠方の構築や光の更新も `/uebridge performance` の待ち数で確認できます。短い停止を見て再取り込みを連打せず、進捗と待ち理由を確認してください。

クリエイティブの飛行は設定済みジャンプキーの二度押しで切り替え、ジャンプで上昇、しゃがみで下降します。サバイバルでは飛行できません。モード変更時も解除します。

`/uebridge import start` のやり直しはUE地形の再取り込みです。UEで行った設置・破壊を置き換えるので、通常の移動や描画距離変更のたびに実行しません。

## 6. モブ・光・投下を確認

### モブ

ゾンビ・村人のスポーンエッグで、空きのある地面を右クリックします。生成位置をモブの寸法から決め、プレイヤー・床・壁による妨害、テンプレート照合、Actor生成、モデル初期化を診断します。

Minecraft標準の `/summon minecraft:zombie`、`/summon minecraft:villager` で初期取り込み後に追加した個体も検出対象です。UE転送の成功ACKを受けてから元MC個体の動作を一時停止し、停止・切断時に戻します。

**標準 `/summon` の `~ ~ ~` はMCプレイヤーの座標です。** UE操作で遠くへ移動しても、元MCプレイヤーの座標とは一致しません。現在のUE位置の前方へ直接出したい場合はこちらを使います。

```text
/uebridge mobs summon minecraft:zombie
/uebridge mobs summon minecraft:villager
/uebridge mobs
```

対応する地上種の基本モデル・歩行・汎用AIが対象です。モブ固有AI、飛行／水中種、全描画層・装備・戦利品まで完成させたものではありません。

### UE照明とバニラ風の光

```text
/uebridge video exposure 0
/uebridge lighting on
/uebridge lighting off
```

ONはUEの光・影・反射を使います。OFFの設定を共通素材へ固定せず、切り替えて戻したときにUE用の出力へ戻します。法線と面の向きを照合するチェックも加えました。実機画像の違和感を法線だけが原因と断定したものではありません。時間AAとモーションブラーは配信映像では停止し、腕の残像を避けます。

OFFは一定の明るさから、Minecraftの光環境を使う表示へ変更しました。面の方向、角のAO、空の光、ブロック光、昼夜、天候、ディメンション、Minecraftの明るさ設定を反映します。腕・持ち物・モブ・投下アイテムも周囲光を使います。実際のMCの空との合成を継続します。

初期地形はMCの光・光源・遮光情報を利用します。UEで松明などを設置・破壊した後は、UE管理の地形で光を伝播・更新します。遮光するフルブロックと、スラブ等のネイティブの面も使います。伝播とメッシュの光の更新は処理量を制限して進めるため、大きな初期取り込みや大量の同時編集では短い更新待ちが生じます。

同じ場所でON→OFF→ON、昼と夜、屋外と屋内、松明の設置と破壊、壁越しの遮光を比較してください。これはネイティブ情報を使うバニラ風の実装であり、全ブロックの全状況でMinecraft単体と画素単位に一致することを確認したものではありません。

### アイテム投下

インベントリを閉じ、設定済み投下キーで主手の持ち物を1個、Minecraft標準のCtrlを併用するスタック投下でまとめて投下します。UE操作中のインベントリ画面からの投下・画面外へのドラッグは、未転送のMC個体や所持数の不整合を防ぐため受け付けません。UEが初速・重力・地面と壁の衝突、回転、浮き、合流、寿命、拾得を処理します。

MCインベントリとUEの投下個体をID付きで対応させ、生成拒否・再送・拾得・切断で同じ処理を二重にしないようにします。投下前後と拾得後の所持数を確認してください。主手・地上の静的モデルが対象で、オフハンド表示や使用中のモデル変化・光沢などの従来の制限は残ります。

## 7. RTX 5060でGPU共有を試す

最初の素材取り込みとレベル確認が終わったら、UEのPlayを停止して `/Game/UE` を保存し、UE Editorを閉じます。

使用中フォルダーの **Launch-UEBridge-GPU.cmd** を実行します。UEのインストール場所を入力すると、保存済み `/Game/UE` を **ゲーム起動・D3D11・1920×1080** で開きます。EditorのPlayボタンを押す手順ではなく、ゲームがすでに起動した状態です。その後、MC側で初期取り込み→READY→UE操作の手順を行います。

別の保存済みレベルを使う場合は、同じフォルダーのPowerShellで次を実行します。

```powershell
.\Launch-UEBridge-GPU.ps1 -Level /Game/YourSavedLevel
```

GPU用のDLLはMODへ同梱し、自動で展開します。手動のDLLコピーやネイティブビルドは不要です。MCとUEが同じNVIDIA GPUを使う必要があります。D3D11とOpenGLの共有拡張・ドライバー対応を実行時に確認します。

- `auto`：共有できればGPU経路、条件が合わなければJPEGへ戻ります。
- `jpeg`：比較用の従来のJPEG＋TCP経路を使います。
- `gpu`：GPU経路を要求します。条件不成立時も診断を出してJPEGへ戻ります。

```text
/uebridge video transport auto
/uebridge video transport jpeg
/uebridge video transport gpu
```

**ゲーム起動でMCへ映像を配信中は、UE側のウィンドウが黒くなることがあります。** UEのメイン画面の重複描画を止め、配信用SceneCaptureを描画しているためです。MCに映像が来ていれば、この黒いUE画面だけで失敗とは判断しません。配信を止めたら元の描画設定へ戻します。EditorのPIEプレビューはこの省略の対象外です。

GPU共有はD3D11経路です。UEの通常のD3D12／Lumenを使う場合はJPEG経路を使います。D3D11のUE照明とD3D12のLumenでは影・GIの見え方が異なることがあります。GPU経路はJPEG圧縮、CPU画像転送・復号を避けますが、GPUのコピーと同期は必要です。最終的なFPSと遅延は実機で測定します。

## 8. 4チャンクから6チャンクへ測定

4チャンクで通常の移動・モブ・アイテム投下・昼夜と光を確認してから、次で6チャンクへ伸ばします。

```text
/uebridge performance target 6
/uebridge video fps 30
/uebridge performance
/uebridge video
/uebridge status
```

4～6チャンクは水平の目安で64～96ブロックです。実装は8ブロックセルの配置に合わせ、中心セルを含む約68／100ブロックの水平半径を取り込みます。上下は中心付近104ブロックの帯を使い、UEの現在位置に追従してストリーミングします。Minecraftの全高さ・全世界を一度に保持する設定ではありません。UE編集は同じセッションの再読み込みにも保持します。

描画では隠れた面を除去し、アトラスで素材の切り替えを減らします。近傍の衝突と遠方の見た目を分け、編集・光の更新を必要なセルへ限定します。ソースとなる専用MCのチャンク取得もUE位置に追従し、終了時に専用の取得要求を解除します。MCブロックを編集してUEへ合わせる仕組みではありません。専用MCワールドの未生成の場所へ進んだ場合は、Minecraftの通常の新規チャンク生成が行われる場合があります。

判断に使う数値：

- **受信したUE映像FPSと表示へ渡したFPS**。MinecraftのFPS表示とは別です。
- UEのフレーム時間、面数・描画セクション数、映像準備時間（JPEGの読み戻し／GPU完了待ちを含む）、MCの復号・アップロード、GPU共有が使えない場合の理由。これはGPU実行時間を直接測る計測器ではありません。
- 入力からMCの描画へ渡すまでのp50／p95。モニターが実際に発光するまでの遅延そのものを測った値ではありません。

同じ場所・同じ解像度・同じ距離でJPEGとGPUを比べます。30fpsでは1フレーム33.3msですが、FPSと遅延は別です。瞬間的なMCのFPSが30以上でも、受信映像が30fps未満なら目標達成としません。

## 不具合時に添付するもの

- モブ：失敗直後の `/uebridge mobs`、`/uebridge status` とUE出力ログの `Bridge mob` 周辺。試した種類・卵／標準summon／Bridge専用summonの別も記載。
- アイテム：書き出し完了、取り込み時のTraceback全体、`held model` の診断。投下なら投下前後の個数も記載。
- 光：同じ位置のON/OFF画像、昼／夜・松明の有無、GPU用D3D11か通常D3D12かを記載。
- 性能：`/uebridge performance`、`/uebridge video`、`/uebridge status` のスクリーンショット。解像度・目標距離・実際の転送方式・受信／表示FPSを含めます。

変更は実装とクラウドで可能な検証の範囲です。実機受け入れでは、ゾンビ・村人が1体だけ生成し歩くこと、昼夜・光源・影、投下と所持数、1080p・4～6チャンクの実映像30fps以上を確認します。

## クラウドでの検証

MOD 0.11.0のビルド／リマップ、Java 152件・Python 65件が成功しました。ローカルのMinecraft 1.21.11クライアント素材を一時的に使い、1008ブロックIDのモデル・テクスチャ解決とmanifest検証を確認しました。独立したC++計算で、腕・輪郭・映像マスク、光伝播と遮光、地形の衝突箱・法線、モブの生成候補を検証しています。映像撮影のタイミングも12項目で検証し、UEの更新頻度が42Hzでも30fps設定を21fpsへ落とす処理を修正しました。Windows x64のJNI DLLとGPU転送のネイティブ部分はクロスコンパイル成功です。

UEモジュールのビルド、UE Pythonの実エディターでの実行、モブ・投下のUE統合テスト、GPU共有の実ドライバー動作、RTX 5060での目標FPSは未実施です。導入後は上記の順で確認します。

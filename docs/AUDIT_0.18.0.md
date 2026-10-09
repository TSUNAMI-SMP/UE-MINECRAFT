# 0.18.0 検証記録

2026-10-09。Minecraft Java 1.21.11／UE 5.8向け。UE単独プレイを保ち、4種類の追加物理アイテム・生成マテリアル・整理と保存・シーン記録と60fps AVI出力を実装しました。0.17.0までのソースを含む累積更新です。

## 実行して成功した検証

- Java 21／Fabric Loomの `test build --offline`。JUnit **219件、失敗・エラー・スキップ0**。0.18.0 JARを生成。元のUIアイテムを変更せず、4つのUE専用識別子がモデル／アイコンを共有して追加されることを検証。
- `bridge/tests` Python **119件、失敗0**。素材・形式・失敗時の保持等の既存検証。
- `tools/test_native_*.py` **55件、失敗・スキップ0**。PowerShellをPATHに入れて設定保持も実行。追加アイテムの取り込み、既存の裏面追加・セル連結・符号付き角度と戦闘の回帰確認を含む。
- Minecraftコード取得ツール **7件、失敗0**。
- 本番の `BridgeRealisticPhysics.h` をC++17・警告をエラー扱いでコンパイルして実行。負の格子座標、失敗時の保持、900 tickの水量保存、正確な1杯の回収、飛沫の量保存、水／溶岩の固化量、砂512粒の接地と支持除去、同一tick列の決定性、4秒導火線、連鎖爆発、個数上限、重複TNT配置の拒否、種類と半径による削除を検証。
- 本番の `BridgeMjpegAvi.h` をコンパイルしてAVIを生成。ffprobeで**MJPEG・60/1 fps・120フレーム**を照合し、ffmpegで全フレームをデコード。容量不足時の追加拒否と、1フレームだけの正常な索引終端も確認。
- 生成マテリアルのグラフ検証 **1件**。砂・TNT・固化物・水・溶岩・火・煙のLit／Simple計14種類、保存と再実行、反射／屈折用入力、物理時計による液体画像アニメーションを確認。UE APIを模したテストで、GPUシェーダーのコンパイル結果ではありません。
- PowerShell録画ランチャーの構文と検証関数。正常入力、録画容量／秒数／フレーム数上限、60fps・無音条件、必須ファイル、MD5・破損の拒否。既存の取り込みログ・完了マーカー・ソースハッシュ検証も成功。
- 既存の独立C++計算14スイート：レシピ、攻撃、移動・しゃがみ、感度、モブ制御、光、空、メッシュ、アウトライン、インベントリ、召喚位置、映像周期／マスク。native render 1792ケース・5376 RGBチャンネル、空714、character73、outline8、マスク15往復等が成功。
- 本番Java素材書き出しをローカルの1.21.11 client JARへ適用し検証。**1168 ID・2215モデル・1077画像・除外0**。専用アイテム形状を使う代替表示151 ID。すべての専用rendererやゲーム動作の確認ではありません。
- Python構文、差分の空白、JAR／ZIPのCRC・SHA-256、バージョン・新規ソース・15ヘルパー・録画ランチャー・導入ガイドの同梱内容を確認。

## 追加したUEテスト（この環境では未実行）

`UEBridge.Realistic.SaveAndCleanupTransactions`：砂の睡眠状態、水・溶岩・液滴の量、固化物、TNTの速度と導火線を保存・復元。不正な最後の行でも全体を変更しないこと、種類別削除、取り消し、全物理の削除を確認します。

この環境にはUE Editor・Windows・GPUがなく、このUEテストと0.18.0のモジュールビルドは実行できていません。独立C++テストが通ったことは、UE API・衝突・入力・マテリアル・実描画の統合検証を意味しません。

## 実装上の範囲

- 液体は25cm格子の有限体積、砂は接触する球粒子。SPH／FLIP、体積煙、高密度な撮影用流体の完成版ではありません。火・煙は独自メッシュで表示します。
- 見た目と計算を分離し、既定medium・ON。CPUの60Hz固定刻みを使い、低fpsで残った計算は後続フレームへ保持します。リアルタイム60fpsや特定GPUでの性能を確認していません。
- 記録は20Hzを上限とした観測列です。別プロセスで1/60秒ごとの補間状態を再描画し、無音・SDRのAVIへ保存します。未観測の入力／接触、全GUI・全バニラ粒子／エンティティの復元は対象外です。
- リプレイは固定した基準ワールドを読み込み、通常の保存を優先しません。操作・AI・ワールド規則・自動保存・終了時保存を止め、地形の隣接面と光の更新を終えてからフレームを書き出します。
- 書き出しや録画のデータ・ハッシュ・上限を検証し、不正時は停止します。未完成の録画／動画は `.part` とログを残します。

## 再実行

環境準備は [CLOUD_SETUP.md](CLOUD_SETUP.md)、導入と実機確認は [UPGRADE_0.18.0.md](UPGRADE_0.18.0.md) を参照します。

```bash
env JAVA_HOME=/workspace/toolchains/jdk-21.0.12.1 \
PATH=/workspace/toolchains/jdk-21.0.12.1/bin:$PATH \
GRADLE_USER_HOME=/workspace/gradle-cache \
minecraft-mod/gradlew -p minecraft-mod test build --offline --no-daemon --max-workers=2
python -m unittest discover -s bridge/tests -p 'test_*.py'
env PATH=/workspace/toolchains/powershell-7.4.19/opt/microsoft/powershell/7:$PATH \
python -m unittest discover -s tools -p 'test_native_*.py'
python tools/test_realistic_physics.py -v
python tools/test_realistic_materials.py -v
pwsh -NoProfile -File tools/test_replay_manifest.ps1
pwsh -NoProfile -File tools/test_native_import_log.ps1
python -m compileall -q tools bridge
git diff --check
```

生成マテリアル、UEの型とAPI、衝突、読み込み・保存の統合、動画の手・HUD合成、ゲーム画面の色と透明度、実FPSはWindows UE 5.8.3で確認する必要があります。Minecraftの元JAR・原作コード・画像や、例の動画の素材は配布していません。

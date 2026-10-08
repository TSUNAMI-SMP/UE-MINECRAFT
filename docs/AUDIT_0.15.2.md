# 0.15.2 検証記録

2026-10-08。0.15.1のビルド修正を含みます。

- Java 21 / Fabric Loom：`./gradlew test build --offline --no-daemon --max-workers=2` 成功。JUnit 203件、失敗0、スキップ0。
- bridge/tests Python：114件成功。手持ち照明のビュー基準入力・バニラ描画切替をマテリアルグラフの検証へ追加。
- native UI Python：20件成功。
- native setup Python：20件中19件成功、1件は環境依存でスキップ。
- 地形メッシュ：128種のランダムなコライダー往復と6面の向き検証成功。実際のメッシュが使うUEFrontQuadの頂点順で、外向き法線と時計回りの表面判定を別々に検証。
- character production math：73件成功。
- combat/movement、mob AI、outline 8チェック、lightmap 1792ケース/5376色チャンネル、voxel lighting成功。
- ローカルのMinecraft 1.21.11 JARからブロック素材の検証成功：1008ブロック、2055モデル、1075テクスチャ。専用レンダラー/不可視など160種は既存の除外。
- Python編集ファイルの構文、git diff --check成功。
- MODとZIPのCRC、配布物と作業ファイルの一致、SHA-256をパッケージ作成時に確認。

MinecraftのGuiRenderer.prepareItemInitiallyが外側へ(size,-size,size)を適用することを参照し、GUIアイコンの法線計算にY反転を補いました。バニラJARや素材を配布物へ追加していません。

Windows UE 5.8.3のC++実ビルド、ShaderCompileWorkerでの新しい照明グラフ、CharacterMovementの実ノックバックと死亡接地、GPUの面・文字描画、マウス入力、実FPSは未実行です。数学・グラフ・パッケージの検証成功は実機確認の代わりにはなりません。

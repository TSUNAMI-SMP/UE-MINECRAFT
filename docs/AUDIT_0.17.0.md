# 0.17.0 検証記録

2026-10-08。Minecraft Java 1.21.11／UE 5.8向け。UE単独プレイを維持し、描画・外見・クラフト・精錬・装備・ワールド更新・基本レッドストーンと搬送を追加しました。0.16.2までの修正を含みます。

## 実行して成功した検証

- Java 21／Fabric Loomの `test build --offline`。JUnit **218件、失敗・エラー・スキップ0**。0.17.0 JARを生成。
- `bridge/tests` Python **119件、失敗0**。生成マテリアルの色・光・glint・アニメーション配線、形式・不正データ・取り込み失敗処理。
- `tools/test_native_*.py` **54件、失敗・スキップ0**。PowerShell 7.4.19をPATHに入れ、起動スクリプトの設定保持も実行。レシピ・コンテナ・防具画像・粒子フレームの検証を追加。
- Minecraftコード取得ツール **7件、失敗0**。
- 本番の独立C++レシピ計算：定形の移動と反転、不定形の重複タグ候補、余分な材料の拒否、精錬種別、接地ノックバックの通常／ダッシュ値。
- combat/movement、movement parity、character math73チェック、mob AI math。通常・追加攻撃、20 tickの空中計算、しゃがみ端判定、感度・飛行等。
- light math、native render **1792ケース／5376 RGBチャンネル**、sky math714チェック、meshing128種の衝突往復と面の向き、outline8チェック、インベントリ数量保存。
- 本番Java素材書き出しを1.21.11のローカルclient JARへ適用し、Pythonで検証。**1168 ID、2215モデル、1077画像、除外0**。専用アイテム形状を使う代替表示は151 ID。全状態・全専用rendererの実行検証ではありません。
- プリズマリンの300 tick×22フレーム補間を、105600ピクセルの画像へ展開せず352ピクセルのフレーム列とシェーダー補間へ変換する回帰検証。短い非等間隔の補間はtickごとの画像へ変換。
- 裏面追加の本番C++96ケース、セルの別ファイル連結と2051座標ケース。以前のTArray自己参照クラッシュ対策を保持。
- Python構文・差分の空白検証。配布JAR／ZIPのCRC・SHA-256・新規ソース・ヘルパー・JAR同梱内容を検証。

生成グラフのテストはUE APIを模した厳密な検証です。ShaderCompileWorkerやGPUの描画確認ではありません。参照に使用したMinecraft JAR、原作クラス、画像は配布物へ含めていません。

## 追加したUEテスト（未実行）

- `UEBridge.Native.Crafting.ConservationAndResume`：材料と結果の数量、半分精錬して保存・再開、ホッパーの1個搬送、排出失敗時の保持、かまどの面別入力、コンテナ型の上書き拒否。
- `UEBridge.Native.Rules.AtomicMovementAndGateDirection`：セルをまたぐ移動、範囲外・重複所有位置の失敗時の原状態保持、リピーター／オブザーバーの入力・出力方向。

この環境にはUE Editorがなく、これらのUEテストと今回のWindows UE 5.8.3 C++ビルドは実行できていません。

## 再実行

環境準備は [CLOUD_SETUP.md](CLOUD_SETUP.md) を参照します。

```bash
env JAVA_HOME=/workspace/toolchains/jdk-21.0.12.1 \
PATH=/workspace/toolchains/jdk-21.0.12.1/bin:$PATH \
GRADLE_USER_HOME=/workspace/gradle-cache \
minecraft-mod/gradlew -p minecraft-mod test build --offline --no-daemon --max-workers=2
python -m unittest discover -s bridge/tests
env PATH=/workspace/toolchains/powershell-7.4.19/opt/microsoft/powershell/7:$PATH \
python -m unittest discover -s tools -p 'test_native_*.py'
python tools/test_recipe_math.py
python tools/test_combat_movement_math.py
python tools/test_movement_parity_math.py
python tools/test_character_math.py
python tools/test_mob_ai_math.py
python tools/test_lighting_math.py
python tools/test_native_render_math.py
python tools/test_sky_math.py
python tools/test_meshing_math.py
python tools/test_outline_math.py
python tools/test_native_inventory_math.py
env JAVA_HOME=/workspace/toolchains/jdk-21.0.12.1 GRADLE_USER_HOME=/workspace/gradle-cache python tools/test_block_resources.py
git diff --check
```

## 実機で残る検証と機能

Windows UEコンパイル、シェーダー、手のHDR合成と透明度、元のモブFeatureRendererの実取得、全装備とGUIの実描画、IME、衝突と戦闘、回路と搬送の統合、実FPSは未検証です。

全Data Components・エンチャント効果・動的レシピ・全専用機能ブロック・全レッドストーン規則・全モブAI・流体規則を移植し終えた版ではありません。対応と制限は [UPGRADE_0.17.0.md](UPGRADE_0.17.0.md) と [NATIVE_PLAY.md](NATIVE_PLAY.md) に記載しています。

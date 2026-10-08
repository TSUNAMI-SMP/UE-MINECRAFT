# 0.16.0 検証記録

2026-10-08。対象はMinecraft Java 1.21.11／UE 5.8。0.15.4のソースを基に、ネイティブ操作・HUD・戦闘・流体・時間と保存を変更しました。

## 成功した検証

- Java 21／Fabric Loomの `test build --offline` 成功。JUnit **210件、失敗・エラー・スキップ0**。配布用0.16.0 JARを生成。
- `bridge/tests` Python **119件、失敗0**。流体アニメーションの形式・寸法・不正型、アイテムNONE形状と既定スタック参照、インポートの失敗時のPalette復元も検証。
- native setup Python20件中19件成功・環境依存1件スキップ。native UI20件、native world format8件成功。時刻・時間サイクル・天候と回収待ちを含む保存／不正値拒否を検証。
- production combat/movement：攻撃チャージ・無敵時間・通常と追加ノックバック・死亡・範囲攻撃、空中の速度と変位、液体の速度と変位を検証。30・60・120・144 FPSで20 tickの独立参照計算と一致。
- 提供済みのハスク実測上向き速度0.313600006ブロック/tickと比較。初速0.4、重力0.08、抵抗0.98の計算が一致。UEの実衝突を測定した結果ではありません。
- movement parity：前後・斜めのしゃがみ端判定、しゃがみ目線、飛行抵抗・マウス曲線成功。character math73チェック成功。
- mob AI math：ゴールの時間、障害物ジャンプ、コウモリ・魚の制御成功。全種固有AIのテストではありません。
- sky math714チェック、light math、meshing128種のコライダー往復・6面の向き、outline8チェック、インベントリ数量保存、native render1792ケース／5376 RGBチャンネル成功。
- Minecraftのローカルclient JARにある1168ブロック状態ファイルに対して、本番の素材書き出しとPython検証が成功。**1168 ID、2215モデル、1075画像、除外0**。そのうち**151 IDは専用描画のアイテム形状を使う代替表示の対象**。
- Python構文、差分の空白検証、配布JAR／ZIPのCRC・SHA-256・収録内容とソースの一致を確認。

素材検証は各IDの代表的な合成状態を使います。Minecraft実行中の全状態・全専用rendererの取得成功、UE上での全状態の正しい描画を保証する検証ではありません。Minecraftの画像、ゲームJAR、診断で取得した原作クラスは配布物へ含めません。

## 再実行

`docs/CLOUD_SETUP.md` の通常の準備・検証方法を使用します。今回の環境では準備済みJDK21とGradleキャッシュを使い、ネットワーク取得なしでMODをビルドしました。

```bash
env JAVA_HOME=/workspace/toolchains/jdk-21.0.12.1 \
PATH=/workspace/toolchains/jdk-21.0.12.1/bin:$PATH \
GRADLE_USER_HOME=/workspace/gradle-cache \
minecraft-mod/gradlew -p minecraft-mod test build --offline --no-daemon --max-workers=2
python -m unittest discover -s bridge/tests
PYTHONPATH=tools python tools/test_native_setup.py
PYTHONPATH=tools python tools/test_native_ui.py
PYTHONPATH=tools python tools/test_native_world_format.py
python tools/test_combat_movement_math.py
python tools/test_movement_parity_math.py
python tools/test_native_regressions.py
python tools/test_character_math.py
python tools/test_mob_ai_math.py
python tools/test_sky_math.py
python tools/test_lighting_math.py
python tools/test_meshing_math.py
python tools/test_outline_math.py
python tools/test_native_inventory_math.py
python tools/test_native_render_math.py
env JAVA_HOME=/workspace/toolchains/jdk-21.0.12.1 GRADLE_USER_HOME=/workspace/gradle-cache python tools/test_block_resources.py
git diff --check
```

## 未検証と未完成の範囲

Windows UE 5.8.3のC++コンパイル、ShaderCompileWorker、GPU上での液体・粒子・HUDの表示、IME、実マウス、端の衝突、戦闘・死亡時の衝突、実FPSは未実行です。数学・形式のテストはこれらの代替ではありません。

液体の原作完全一致、全モブ固有AI、すべての専用ブロックの形状と機能は未完成です。具体的な範囲とWindowsでの確認手順は [UPGRADE_0.16.0.md](UPGRADE_0.16.0.md) を参照してください。

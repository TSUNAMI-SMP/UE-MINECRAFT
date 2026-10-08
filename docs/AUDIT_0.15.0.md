# 0.15.0 修正・検証記録

0.15.0は、ユーザー報告と、提供されたMinecraft Java 1.21.11の参照コードを突き合わせて変換した更新です。元のJavaソース、バニラ画像、音声はリポジトリへ追加していません。

| 問題 | 0.15.0の対応 |
| --- | --- |
| 草ブロックの土面欠落・白い草・黒いテクスチャ | ブロック状態ごとの面・法線・透明度・バイオーム色・水占有をexportし、UEの座標位置で色を取得。古いexportは再利用せず再exportする。 |
| 照明ONの時刻、太陽、月、影、空 | 1.21.11の天体角度、光マップ、空・ブロック光、夕焼け、星、月相を変換。ONはUEの影、OFFはMinecraft光マップを併用する。時刻はexport時点のスナップショット。 |
| インベントリの順番・カテゴリ・削除・装備 | ItemGroupsの表示順とカテゴリ、36枠、オフハンド、カーソル、装備枠、削除枠、ドラッグ／分割操作を追加。コンポーネント違いの全スタックは未対応。 |
| 攻撃のクールダウン・音・赤表示・無敵時間 | 攻撃速度から20/速度tickを計算し、HUDは表示用の時刻、攻撃は半tickをサンプル。強弱・空振り・クリティカル／ノックバック／スイープ音、10tickの受付間隔、赤いhurt overlayを変換。 |
| ノックバック・死亡・空中の死体 | 抵抗、速度半減、接地時の上向き上限、死亡20tick、元の速度・重力、LivingEntityRendererの倒れる曲線、20個のpoof粒子を実装。凹凸床での最終表示はWindows実機確認が必要。 |
| 右クリックの手振り・しゃがみ・飛行慣性・視点 | 成功した使用だけ手振り、20Hzのしゃがみ目線・端保持、Minecraft感度曲線とスムーズカメラ、飛行の水平／垂直慣性、三人称切り替えを整理。 |
| モブ速度・アニメーション・AI | exportした速度・防具・抵抗・水占有を使用し、20Hzの目標更新、歩行フレーム、障害物一段ジャンプ、魚・イカ・コウモリの基本制御を追加。全種の固有AIではない。 |

## 自動検証

- `python tools/build_mod.py test build` — Java 21のMODテストとjarビルドに成功。
- `python -m unittest discover -s bridge/tests -p 'test_*.py'` — 114件成功。
- `python tools/test_native_setup.py` — 20件成功、Linux固有の1件をskip。
- `python tools/test_native_ui.py` — 20件成功。
- `python tools/test_native_world_format.py` — 7件成功。
- `python tools/test_character_math.py` — 73件成功。
- `python tools/test_movement_parity_math.py`、`test_combat_movement_math.py`、`test_sky_math.py`、`test_lighting_math.py`、`test_native_render_math.py`、`test_mob_ai_math.py`、`test_native_inventory_math.py`、`test_meshing_math.py` — すべて成功。光マップは1,792条件 / 5,376チャンネル、空は714条件を照合。
- UEの自動テスト、WindowsのC++／マテリアルコンパイル、RTX 5060での画面・音・入力・実FPSは未実行。

参照コードとの差分は `docs/COMBAT_REFERENCE_0.15.0.md` と `docs/VANILLA_RENDER_REFERENCE_0.15.0.md` に記録しています。完全なMinecraftクライアント移植ではなく、UE側で動かせるデータと計算へ変換した実装です。

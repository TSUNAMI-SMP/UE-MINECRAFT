# 1.21.11 描画処理の参照と変換

今回ユーザーが取得した Minecraft Java 1.21.11 の参照コード・シェーダーを読み、次の処理を UE 用に変換した。参照コードやバニラ画像そのものは配布物へ追加しない。

| 参照する元の処理 | UE 側の変換 |
| --- | --- |
| `RenderPipelines.POSITION_TEX_COLOR_CELESTIAL` / `BlendFunction.OVERLAY` | 太陽・月の `SRC_ALPHA, ONE` 加算合成。普通の透過合成が黒い背景を置き換えていた。UE の線形色で合成するため、表示 RGB で加算した結果の線形差分を出力する。 |
| `SkyRendering.renderCelestialBodies`, `createMoonPhases` | 天体の回転をブリッジの座標変換 `MC(z,-x,y)` に合わせる。月の UV は太陽に対し両軸を反転する。 |
| `SkyRendering.createStars`, `Random.create(10842L)` / `CheckedRandom` | 同じ48ビット乱数列、1,500回の試行・棄却条件、サイズ・回転による780個の星の四角形。新しい書き出しの星の角度・明るさを使用する。 |
| `SkyRendering.createSunrise`, `renderGlowingSky` | 16分割の色付き三角形ファン。書き出した `SUNRISE_SUNSET_COLOR_VISUAL` のアルファを透明度と形状へ適用する。 |
| `LightmapTextureManager.update` / `lightmap.fsh` | 空・ブロックの明るさ、環境色、暗闇、ガンマ、暗転・暗視の順序を保持。16×16 RGBA8 光マップの量子化も適用する。End の環境色 `(0.99,1.12,1.0)` は RGB 整数へ切り詰めず書き出す。 |
| `terrain.fsh`, `entity.vsh`, `entity.fsh` | UE の sRGB テクスチャを表示 RGB へ戻し、バイオーム色・面の陰影・光マップを乗算してから最終色を一度だけ線形へ戻す。光マップだけを先に線形化して乗算する方式の色のずれを修正する。 |
| `DiffuseLighting.updateLevelBuffer` / `minecraft_mix_light` | エンティティの面の陰影は正規化した2方向の光と `min(1, 0.4 + 0.6×受光量)`。地形の頂点 AO・面の陰影はそのまま使う。Nether の2つ目の光は Y を反転する。 |
| `OverlayTexture`, `entity.fsh` | `0xb2ff0000` の被ダメージ色を、面の陰影の後・光マップの前に合成する。赤の比率は `77/255`。 |
| `InGameHud.renderCrosshair` / `RenderPipelines.CROSSHAIR` / `BlendFunction.INVERT` | バニラの画像を背景 RGB に対し `source×(1-background) + background×(1-source)` で合成する。攻撃インジケーターを含めた3枚の画像と部分 UV に対応する。UE の画面合成後マテリアルを使う。 |

検証:

- `python tools/test_sky_math.py`: 天体の角度・座標・月相と、独立した Java 乱数実行で取得した星の個数・位置・サイズの照合。
- `python tools/test_native_render_math.py`: 実際に生成する光マップ HLSL の算術を C++ のベクトル補助で実行し、昼・夜・ガンマ・暗視・暗闇・Nether/End の1,792条件、5,376色成分を参照シェーダーの独立評価と照合。
- Python のマテリアルグラフ契約テスト: 加算合成、UV、RGBAの4成分、UE画面位置出力、被ダメージ合成の順序、最終色変換を確認。

制限:

この環境では Windows UE 5.8 のシェーダーコンパイルと画面比較を実行できていない。実機では特に画面合成後の照準、雨・夜の天体、夕焼け、色の比較を確認する必要がある。

環境は書き出した位置・時刻・天候のスナップショットで、UE 内で Minecraft の時間・天候を進める処理は含まない。雲、完全な距離・水中の霧、End の専用スカイボックス・閃光の描画は再現済みとは扱わない。夕焼けの透過処理と地形・天体の重なりも実機比較が必要。

照明 OFF の光マップ表示をバニラ比較の基準にする。照明 ON の UE の影・物理ベース照明は独自の追加表示で、Minecraft の平坦な描画処理と同一とは扱わない。過去の白い色かぶりを増やさないよう、固定の暖色の太陽・青色の月光を削除している。

# 0.16.1 検証記録

2026-10-09（日本時間）。利用者のWindows UE 5.8.3／Visual Studio 14.44のビルドログを確認しました。0.16.0は43アクション中、BridgeReceiverNative.cpp、BridgeVideo.cpp、BridgeWorldFluids.cppのエラーでリンク前に停止しています。ゲームは起動せず、素材の再取り込みも始まっていません。

修正したのはBridgeReceiverNative.cpp、BridgeVideo.cpp、BridgeWorld.cpp、BridgeWorld.h、BridgeWorldFluids.cppの5ファイルです。MOD・Pythonインポーター・素材形式は変更していません。

## 実行した確認

- `python tools/test_native_cell_linkage.py` 成功。実ソースから共通セル関数の宣言・定義を取り出し、独立した2つのC++翻訳単位からリンクして呼び出す。-1025～1025の2051ケースで3軸の床除算とセル内座標0～7を確認。
- `python tools/test_combat_movement_math.py` 成功。攻撃・ノックバック・空中／液体中の速度と変位の既存参照比較を維持。
- `python tools/test_native_render_math.py` 成功。1792ケース／5376 RGBチャンネルの描画計算を維持。
- 差分の空白検証、修正ZIPのCRC、収録したソース／ヘルパーと作業ファイルの一致、SHA-256を確認。

セルのテストは最小限の数値・ベクトル型を使う移植可能なC++検証です。UEのコンテナ、ヘッダー、UnrealBuildTool、モジュール全体をビルドしたものではありません。

## 未実行

Windows UE 5.8.3の修正後のC++コンパイル・リンク、取り込み、ShaderCompileWorker、GPU描画、入力、実FPSは未実行です。元のログで確認できたコンパイルエラーに対する修正であり、Windows上のビルド成功を確認済みとは扱いません。

0.16.0での機能の対応範囲・残る機能と広い検証記録は [UPGRADE_0.16.0.md](UPGRADE_0.16.0.md)、[AUDIT_0.16.0.md](AUDIT_0.16.0.md) を参照してください。

# MOD・取り込みスクリプト修正0.9.3

`setup_minecraft_visuals()` の `Invalid state property` と、地形転送の状態名を修正します。
MODとPython補助ファイルの更新です。UEソースは0.9.0＋累積ビルド修正0.9.2のまま使用します。
今回の修正だけのためにUEを再ビルドする必要はありません。

## ダウンロード

- [MOD 0.9.3](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/minecraft-ue-bridge-0.9.3.jar)
- [UE取り込みスクリプト更新ZIP 0.9.3](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/UEBridge-texture-import-fix-0.9.3.zip)

配布ブランチは引き続き `ue-bridge-0.9.0`。旧配布物は保持します。

## 原因

書き出し側が `Property.name(value)` を使わず、Javaの `value.toString()` を使っていました。
たとえばボタンの設置面は、モデル定義では `face=wall` ですが、旧版は `face=WALL` と出力します。
Pythonの状態名検証で拒否されるほか、UEのモデル選択・状態検索でも不整合が起きます。

修正後はMinecraftのプロパティの保存用の名前を使います。全状態の素材書き出しと地形転送で
同じ処理を使います。既存の大文字を含む出力を手で小文字に変換する方法は不要です。
取り込み側の検証は維持し、エラーには対象ブロックID・状態名・再書き出しの指示を追加します。

## 適用手順

1. Minecraftを終了し、UEのPlayを停止します。
2. `C:/UEBridgeTest/MC-Test/mods/` の旧 `minecraft-ue-bridge-*.jar` をゲームフォルダー外へ退避し、
   新しい `minecraft-ue-bridge-0.9.3.jar` を1個だけ置きます。Fabric APIは保持します。
3. 更新ZIPを別のフォルダーに展開します。中の **import_minecraft_textures.pyだけ**を次へ上書きします。

   ```text
   C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/
   ```

   Content/Config/Saved、Source、レベル、既存素材を削除しません。UE再ビルドは不要です。
4. 専用Minecraftのテストワールドを開き、次を実行して完了を待ちます。

   ```text
   /uebridge textures export
   ```

   **旧MODで書き出した素材では解消しないため、必ずMOD更新後に再書き出しします。**
   スキン・モブの出力はこの修正だけを理由にやり直す必要はありません。
5. UEではPlayを停止したまま保存済みレベルを開き、BridgeReceiverが1個であることを確認します。
   レベルを保存し、Output LogのPython入力で次の2行を1行ずつ実行します。

   ```python
   exec(open("C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/import_minecraft_player.py", encoding="utf-8").read())
   setup_minecraft_visuals("C:/UEBridgeTest/MC-Test")
   ```

   最新のブロック書き出しを自動選択します。古い書き出しフォルダーを削除する必要はありません。
6. 赤いPythonエラーがないことを確認してレベルを保存し、UE Play → `/uebridge import start`
   → READY確認 → `/uebridge control ue` の順に開始します。

## 確認範囲

MODビルドとJava97件・Python42件のテストが成功。Minecraft 1.21.11の実際のプロパティ型を使い、
14種類・58値を保存用の名前へ変換し、再解釈できることを確認しました。
7値は旧方式のJava列挙名と異なり、ボタンの `WALL -> wall` も再現・修正しています。
これは全ブロック状態をゲーム内で書き出した統合確認ではありません。
クラウドにUEとゲームGUIがないため、UE取り込み・描画・実機統合は未確認です。

初めて0.9系を導入する場合は、[0.9.0の手順](UPGRADE_0.9.0.md)でUEを更新し、
[UEビルド修正0.9.2](BUILD_FIX_0.9.2.md)も適用したうえで、MODと本補助ファイルには0.9.3を使ってください。

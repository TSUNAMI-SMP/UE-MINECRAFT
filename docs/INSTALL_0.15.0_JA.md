# UEBridge 0.15.0 導入手順（Windows）

このページは、Minecraft 1.21.11のワールドをUEBridgeで取り込み、Unreal Engine上で起動するための手順です。初回は **Minecraft側のMOD導入 → native export → UE側の取り込み** の順に進めます。

## 必要なもの

- Windows 10/11 64-bit
- Minecraft Java Edition **1.21.11**
- Fabric LoaderとFabric API
- Java **21**
- Unreal Engine **5.8**（プロジェクトは5.8系を想定）
- ダウンロードするファイル
  - [minecraft-ue-bridge-0.15.0.jar](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/minecraft-ue-bridge-0.15.0.jar)
  - [UE-Minecraft-MVP-0.15.0.zip（一式）](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UE-Minecraft-MVP-0.15.0.zip)

既存のUEBridgeプロジェクトを使う場合は、一式ZIPの代わりに[UEBridge-update-0.15.0.zip](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.15.0.zip)を使います。

## 1. MinecraftにMODを入れる

1. Minecraftを終了します。
2. `minecraft-ue-bridge-0.15.0.jar`をMinecraftの`mods`フォルダーへコピーします。
3. `mods`フォルダーに、対応するFabric APIも入っていることを確認します。
4. Minecraft Launcherで、**Fabric / Minecraft 1.21.11 / Java 21**のプロファイルを選んで起動します。
5. 対象ワールドを開きます。素材取得用なので、まずシングルプレイで行ってください。

通常の`mods`フォルダーは次の場所です。

```text
%APPDATA%\\.minecraft\\mods
```

別のゲームディレクトリを使っている場合は、そのゲームディレクトリ内の`mods`を使用します。

## 2. MinecraftからUE用データを書き出す

1. ワールドに入った状態で、チャットを開きます。
2. 次のコマンドを実行します。

```text
/uebridge native export
```

3. チャットに完了メッセージが出るまで待ちます。
4. 出力されたフォルダー内に、次のファイルがあることを確認します。

```text
native_manifest.json
```

出力先の例です。

```text
C:\\UEBridgeTest\\MC-Test\\uebridge-export\\native-日時-識別子\\native_manifest.json
```

この`native_manifest.json`の**絶対パスをコピー**しておきます。書き出しが完了したら、Minecraftは終了して構いません。

> 0.15.0では、旧版のexportをそのまま使わず、0.15.0 MODで再度exportしてください。草ブロックの土面、バイオーム色、水、空、装備、攻撃情報などが追加されています。

## 3. UEプロジェクトを配置する

### 初めて導入する場合

1. `UE-Minecraft-MVP-0.15.0.zip`を任意の場所へ展開します。
2. 展開後、次のファイルが存在することを確認します。

```text
UE-MINECRAFT\\unreal\\UEBridge\\UEBridge.uproject
UE-MINECRAFT\\unreal\\UEBridge\\Play-Native.cmd
```

例：

```text
C:\\Users\\<ユーザー名>\\Downloads\\UE-Minecraft-MVP-0.15.0\\UE-MINECRAFT\\unreal\\UEBridge
```

### 既存のUEBridgeを更新する場合

1. Unreal Editorを終了します。
2. `UEBridge-update-0.15.0.zip`を展開します。
3. ZIP内のファイルを、既存の`UEBridge`フォルダーへ上書きコピーします。
4. `Content`、`Config`、`Saved`、既存マップは削除しません。

## 4. 初回の取り込みと起動

### 一番簡単な方法（ダブルクリック）

1. `UEBridge`フォルダーを開きます。
2. `Play-Native.cmd`をダブルクリックします。
3. ファイル選択画面が出たら、手順2で控えた`native_manifest.json`を選びます。
4. Unreal Editorが自動で起動し、ビルド・素材取り込み・専用マップ作成が始まります。
5. 完了後、UEのゲーム画面が起動します。

初回は数分かかる場合があります。取り込み中にMinecraftを起動し直す必要はありません。

### PowerShellで実行する方法

`Play-Native.cmd`で動かない場合は、`UEBridge.uproject`と同じフォルダーでPowerShellを開き、次を1行ずつ実行します。

```powershell
Set-Location -LiteralPath 'C:\\Users\\<ユーザー名>\\Downloads\\UE-Minecraft-MVP-0.15.0\\UE-MINECRAFT\\unreal\\UEBridge'
$manifest = 'C:\\UEBridgeTest\\MC-Test\\uebridge-export\\native-日時-識別子\\native_manifest.json'
.\\Play-Native.ps1 -EngineRoot 'C:\\Program Files\\Epic Games\\UE_5.8' -Manifest $manifest -Rebuild -Reimport
```

`$manifest = ...`だけでは何も始まりません。これはパスを変数に保存するだけです。**3行目の`Play-Native.ps1`を実行して初めて取り込みが始まります。**

Unreal Engineを別の場所へインストールしている場合は、`-EngineRoot`をそのフォルダーへ変更します。通常は自動検出されるため、次の短い実行でも構いません。

```powershell
.\\Play-Native.ps1 -Manifest $manifest -Rebuild -Reimport
```

## 5. 2回目以降の起動

初回取り込みが成功した後は、Minecraftを起動せず、`Play-Native.cmd`をダブルクリックするだけで起動できます。保存したUEワールド、地形編集、位置、インベントリ、モブ状態を再開します。

別のMinecraft exportを取り込むときだけ、`Play-Native.cmd`を起動して新しい`native_manifest.json`を選び、再取り込みします。

## 6. 起動できたか確認する

ゲーム画面で次を確認します。

- ブロックが表示され、床をすり抜けない
- Eでインベントリが開く
- F5で一人称／三人称を切り替えられる
- 左クリックでブロックを破壊、右クリックで設置できる
- クリエイティブでは二段ジャンプで飛行できる
- ゾンビなどのモブをスポーンエッグで出せる
- 草ブロックの上面・側面・土面が表示される

## 7. 失敗した場合に確認する場所

取り込みが失敗したら、UEBridgeを閉じた後に次のログを確認します。

```text
UEBridge\\Saved\\Logs\\NativeImport-*.log
UEBridge\\Saved\\Logs\\UEBridge.log
```

よくある原因は次のとおりです。

- `native_manifest.json`ではなく、フォルダーや別のJSONを選んでいる
- Minecraft側が1.21.11でない、またはMODが0.15.0でない
- Javaが21でない
- `Play-Native.ps1`をコピーしていない
- Unreal Editorや前回のUEBridgeがまだ起動中
- `-EngineRoot`が`UnrealEditor.exe`のあるフォルダーを指していない

`Native setup COMPLETE`がログに出ていれば取り込みは完了しています。`Native setup FAILED`、`Native automation failed`、`Traceback`がある場合は、その前後30行を保存してください。

## 8. 重要な注意

- Minecraftの元ワールドへ変更を書き戻す機能ではありません。UE側の編集はUEの保存へ記録されます。
- 書き出し範囲は有限です。全世界を取り込むものではありません。
- Windows UE 5.8.3の実コンパイル、GPU描画、音声、入力、実FPSは利用環境で確認が必要です。
- クラフト、全コンテナ、全NBT/Data Components、全モブ固有AIなどは完全移植ではありません。

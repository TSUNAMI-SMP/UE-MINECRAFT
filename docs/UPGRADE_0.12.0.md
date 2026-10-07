# 0.12.0：Minecraftから書き出して、UEで直接プレイ

Minecraftの専用シングルプレイから地形・素材・スキン・HUD・操作設定・音をまとめて書き出し、UEの画面で操作します。書き出しが終わればMinecraftを終了できます。UE映像をMinecraftへ送る処理を使わないため、その経路の圧縮による画質低下と転送待ちは発生しません。UE自体の描画FPSや入力遅延は実機での確認が必要です。

初回は **MODとUEを更新 → Minecraftでコマンドを1回 → Play-Native.cmdをダブルクリック** します。次回からは **Play-Native.cmdをダブルクリック** して、同じ書き出しパッケージの保存状態を再開します。

今回の実装はUE単独プレイの基盤と基本操作です。MinecraftのHUD・操作・ゲームルールの完全な移植には達していません。[対応範囲と実機確認](NATIVE_PLAY.md)を確認してください。クラウドではMODビルドと独立した計算・ファイル検証を行い、UE 5.8のWindowsビルド・実描画・IME・実FPSは未確認です。

## ダウンロード

- [MOD 0.12.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.0/downloads/minecraft-ue-bridge-0.12.0.jar)
- [使用中UEプロジェクト用の更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.0/downloads/UEBridge-update-0.12.0.zip)
- [ソース＋MOD一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.0/downloads/UE-Minecraft-MVP-0.12.0.zip)（新規導入用）

すでに使っているUEBridgeには **UE更新ZIP** を使います。配布ブランチは `ue-native-play-0.12.0` です。旧版のダウンロードは保持しています。

## 1. 使用中のMODとUEを更新

1. MinecraftとUEを終了し、使用中のUEBridgeフォルダーを別の場所へコピーしてバックアップします。
2. 専用Minecraft環境の `mods` にある旧 `minecraft-ue-bridge-*.jar` を別フォルダーへ移し、新しい0.12.0のJARを入れます。本MODのJARは1個だけにします。Minecraftは **Java 1.21.11、Java 21、Fabric Loader 0.19.5、Fabric API 0.141.6+1.21.11** を使います。
3. UE更新ZIPを一時フォルダーへ展開します。中の `Source` を、使用中の `UEBridge.uproject` と同じフォルダーへ **統合コピー** し、同名ファイルを上書きします。Sourceフォルダー全体を削除する必要はありません。
4. ZIP内の `.cmd`、`.ps1`、`.py` ファイルも、すべて `UEBridge.uproject` と同じフォルダーへコピーします。

例えば、現在のプロジェクトの場所は次のままで構いません。

```text
C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/
    UEBridge.uproject
    Source/
    Build-UEBridge.cmd
    Build-UEBridge.ps1
    Play-Native.cmd
    Play-Native.ps1
    import_native_play.py
    （ZIP内の残りのPython補助ファイル）
```

**更新ZIPはContent／Config／Saved／UEBridge.uprojectを含みません。** 既存のレベル・設定・素材をフォルダーごと置き換えず、上記のSourceと補助ファイルを更新してください。フォルダー名の `0.2.0` は変更不要です。

新規導入なら一式ZIPを新しい場所へ展開し、`UE-MINECRAFT/unreal/UEBridge` を使用します。UE 5.8と、対応するVisual Studio 2022の「C++によるゲーム開発」・Windows SDKが必要です。初回ビルドはランチャーが開始します。以前このプロジェクトをC++ビルドできていた環境では、そのビルドツールを使用します。

## 2. Minecraftで1回書き出す

1. 更新した専用Minecraft環境を起動し、**LAN公開していないシングルプレイ** の書き出し用ワールドに入ります。最初はクリエイティブを推奨します。
2. 使いたいリソースパック、キー割り当て、マウス感度、FOV、GUIスケール、音量を設定します。持ち込みたいアイテムをインベントリとホットバーへ入れます。
3. UEで開始したい場所に立ち、周囲のチャンクの表示が終わるまで待ちます。Minecraftの描画距離は選ぶ書き出し半径以上に設定します。
4. 従来のUE同期操作が有効なら、先に `/uebridge control off` を実行します。
5. チャットで次を実行し、**書き出し完了** と `native_manifest.json` のパスが表示されるまで待ちます。

   ```text
   /uebridge native export
   ```

既定は水平4チャンクです。初回が動いた後、必要なら `/uebridge native export 5` または `/uebridge native export 6` で別パッケージを作れます。範囲内でも未ロードのチャンクがある場合は、移動・描画距離を調整してチャンクが表示されてからやり直します。書き出し中はワールドを閉じず、開始地点に留まります。

進捗表示は `/uebridge native`、中止は `/uebridge native cancel` です。完了していない途中のJSONはUEへ取り込みません。

書き出し先は、専用Minecraftのゲームディレクトリ内です。例：

```text
C:/UEBridgeTest/MC-Test/uebridge-export/native-日時-識別子/native_manifest.json
```

従来のtextures／player／items／mobsコマンドを別々に実行する必要はありません。この1回で同じパッケージ内へそろえます。ゾンビ・村人の標準テンプレートも書き出し、取り込み時にはモブPaletteの作成・Receiverへの割り当て・レベル保存を確認します。

書き出し完了後はMinecraftを終了できます。**選んだパッケージのフォルダーは、その後も保存してください。** スキン・画像・音は手元のMinecraftから取得し、GitHub配布物には含めません。

## 3. Play-Native.cmdをダブルクリック

UEを閉じたまま、使用中の `UEBridge.uproject` の隣にある **Play-Native.cmd** をダブルクリックします。

初回は、ファイル選択画面で上記の **native_manifest.json** を選びます。UEのインストール先を自動検出できなかった場合だけ、例えば `C:\Program Files\Epic Games\UE_5.8` を入力します。

ランチャーは必要なC++ビルド、素材取り込み、専用レベルの保存、ゲーム起動を順に実行します。初回や更新直後は数分以上かかることがあります。取り込み中のUE画面を手動で閉じず、完了まで待ちます。

自動作成するプレイ用レベルは `/Game/Bridge/Native/NativePlay` です。今まで使っていた `/Game/UE` などの保存済みレベルはそのまま残ります。素材は書き出しパッケージごとに検証し、別の書き出し回の「最新」素材と混ぜません。

ゲームが開いたら地形・足元の衝突の読み込みが終わるまで待ち、画面をクリックして操作します。初期ウィンドウは1280×720です。UEの通常の画面へ直接描画し、MinecraftへのJPEG/GPU映像配信は開始しません。

## 4. 操作と保存

対応している操作は、書き出し時のMinecraftキー割り当てを引き継ぎます。標準設定の例：

| 操作 | 標準設定 |
| --- | --- |
| 移動・視点 | WASD・マウス |
| ジャンプ・しゃがみ・ダッシュ | Space・Shift・Ctrl／前進の二度押し |
| クリエイティブの飛行切替 | ジャンプの二度押し |
| 飛行中の上昇・下降 | ジャンプ・しゃがみ |
| 攻撃／破壊・使用／設置・ブロック選択 | 左クリック・右クリック・中クリック |
| ホットバー | 1～9・マウスホイール |
| インベントリ | E |
| アイテム投下 | Qで1個・Ctrl＋Qでスタック |
| 一／三人称の切替 | F5。Mouse4／Mouse5への割り当ても引き継ぎます |
| HUD非表示・診断 | F1・F3 |
| 一時停止・保存・照明切替メニュー | Esc |

クリエイティブのインベントリにはアイテム検索があります。検索欄をクリックして入力します。日本語検索用にIME入力欄を実装していますが、Windows実機での変換・確定動作は未確認です。左／右クリックでスタック移動・分割、Shiftクリックで移動、数字キーでホットバーへ入れ替えできます。

Escメニューの **照明: ON/OFF** でUE照明とバニラ風の光を切り替えます。切り替えた状態も保存に含めます。

Escメニューの **ワールドを保存** を押し、画面の保存中表示が消えたら **終了** を選びます。通常終了時にも保存を試みますが、強制終了・PC電源断の前には保存操作を行ってください。次回はMinecraftを起動せず、同じ **Play-Native.cmd** をダブルクリックします。

保存先は `UEBridge/Saved/NativeWorlds/<パッケージ識別子>.ndjson` です。地形編集、プレイヤーの位置・視点・体力、インベントリ・カーソル上のスタック、モブ、投下アイテム、着火済みTNTを同じ保存ファイルへまとめます。保存完了時にファイルを置き換える方式で、失敗時は前回の保存を保持します。

**UEでの編集はMinecraftの元ワールドへ書き戻しません。** 新しい書き出しパッケージを選ぶと別のUE保存になります。以前の保存ファイルは保持します。保存範囲・未対応機能は [NATIVE_PLAY.md](NATIVE_PLAY.md) に記載しています。

別パッケージへ切り替える場合は、UEを終了し、同じフォルダーでPowerShellを開いて次を実行します。新しいJSONを1回選択します。

```powershell
.\Play-Native.ps1 -Reimport
```

## エラー時に見る場所

- ビルドで停止：ランチャーに出た **最初のcompiler errorとその前後** を確認します。成功していない旧モジュールでゲームを開始しません。
- 取り込みで停止：`Saved/Logs/UEBridge.log` の `Native setup FAILED` または `Native automation failed` と、その前のTraceback全体を確認します。
- モブが出ない：同じログの `Bridge mob rejected` と、Paletteの名称・appearance／template数を確認します。画像が見つかったことだけでは取り込み完了になりません。
- 保存／読み込みで停止：画面の状態表示と同じログを確認します。問題の保存ファイルと書き出しパッケージを保持して診断します。
- 光・FPS：F3表示と、5秒ごとのUE診断ログを確認します。通常のUE FPSとフレーム時間、地形／衝突／光の処理待ち、照明設定、モブPalette、映像配信がnativeモードで省略されたことを記録します。

`aqProf.dll`・`VtuneApi.dll`などの起動時の開発用DLL警告だけでは、今回のモブ失敗の原因を判断できません。報告済みログの直接の原因は `missing_palette ... palette=None` です。今回はモブ取り込み途中で停止する変数参照を修正し、作成・割り当て・保存まで完了してから起動する構成にしています。

実機での確認手順と残る制限は [NATIVE_PLAY.md](NATIVE_PLAY.md) を参照してください。

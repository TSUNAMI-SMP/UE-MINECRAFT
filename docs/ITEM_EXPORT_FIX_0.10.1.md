# アイテム書き出しの容量修正 0.10.1

`アイテム書き出し失敗: Item manifest exceeds 64 MiB` は、全アイテムの4種類の持ち方を記録すると0.10.0の容量上限を超えるために発生しました。モデル数を減らさず、モデルJSONをgzipで保存するように修正します。
圧縮ファイルのチェックサム・展開サイズ・形状検証を行い、旧版の未圧縮形式も読み込みます。

- [MOD 0.10.1](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.10.0/downloads/minecraft-ue-bridge-0.10.1.jar)
- [Python取り込み修正ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.10.0/downloads/UEBridge-item-export-fix-0.10.1.zip)

UEのC++ソースは0.10.0のままです。**この修正だけのためのUE再ビルドは不要です。** まだUEを0.10.0へ更新していない場合は、先に[0.10.0の手順](UPGRADE_0.10.0.md)のUE更新・ビルドを行ってください。

1. Minecraftを終了します。`C:/UEBridgeTest/MC-Test/mods/` から旧 `minecraft-ue-bridge-0.10.0.jar` を別フォルダーへ移し、0.10.1のJARを入れます。本MODは1個だけにします。
2. UEのPlayを停止します。ZIPを展開し、中の **import_minecraft_items.pyだけ**を次のフォルダーへ上書きコピーします。Content/Config/Saved/Sourceは変更しません。

   ```text
   C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/
   ```

3. 更新したMinecraftを起動し、専用ワールドで `/uebridge items export` を再実行します。表示したい剣等をホットバーに入れ、完了メッセージを待ちます。完了したフォルダーには `manifest.json` と `items.json.gz` が入ります。両方をそのまま保持してください。
4. ブロックとスキンの書き出しは画像では成功しています。そのまま使えます。モブがまだなら `/uebridge mobs export` も実行して完了を待ちます。
5. UEでPlayを停止し、保存済みレベルを保存します。Python入力で次の2行を実行します。

   ```python
   exec(open("C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/import_minecraft_player.py", encoding="utf-8").read())
   setup_minecraft_visuals("C:/UEBridgeTest/MC-Test")
   ```

6. 取り込みが完了したら、UE Play → `/uebridge import start` → READY → `/uebridge control ue` の順で開始します。

失敗した旧itemsフォルダーは完成したmanifestを持たないため、最新取り込みの対象になりません。削除する必要はありません。
今回もローカル素材・スキン・音声を配布物に含めません。
圧縮は書き出しファイル容量と最終保存時の一括メモリ消費への修正であり、ゲーム映像や描画方式は変更しません。
クラウドでは64MiBを超えるデータの書き出し・読み込みをテストしています。Minecraft実機の素材書き出しとUE内での取り込みは未確認です。

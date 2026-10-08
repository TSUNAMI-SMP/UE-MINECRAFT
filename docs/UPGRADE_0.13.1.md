# モブ取り込み修正0.13.1

[UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.13.1/downloads/UEBridge-update-0.13.1.zip)

`NativeImport-20261008-150903-1211d06f974f4079970c064871d0767e.log` ではテクスチャ・アイテム取り込みを通過し、モブの基本姿勢を作る際に `Transform: Cannot nativize 'Quat' as 'Rotator'` で停止しています。PythonのTransformコンストラクターはMakeTransformを呼び、rotationにRotatorを要求します。Minecraftから座標変換したQuatをUEの `Quat.rotator()` で変換して渡します。基本姿勢と歩行16フレームの双方に適用します。

## 0.13.0適用済みの場合

1. UE Editorとゲームを終了します。現在の `import_minecraft_mobs.py` を別の場所へコピーして保管します。
2. 更新ZIPを一時フォルダーへ展開し、直下の **import_minecraft_mobs.pyだけ** を次のフォルダーへコピーして上書きします。

   ```text
   C:\Users\tsush\Downloads\UE-Minecraft-MVP-0.2.0\UE-MINECRAFT\unreal\UEBridge
   ```

3. 同じフォルダーの **Play-Native.cmd** を起動します。更新したPythonを検出して取り込みを再試行します。ファイル選択が出た場合は以前と同じ書き出しの `native_manifest.json` を選びます。

この修正にはC++の変更、MOD交換、Minecraftからの再書き出しはありません。0.13.0の導入が済んでいない場合は先に [0.13.0更新手順](UPGRADE_0.13.0.md) を適用し、この1ファイルを更新してください。ZIPには0.13.0のSourceと補助スクリプトも含まれています。

Content、Config、Saved、uproject、ビルド済みDLLはZIPに含みません。既存のマップ・UE保存・Minecraftの元ワールド・旧配布物を保持します。専用nativeマップの取り込みはステージングで行い、失敗時に成功マーカーを記録しません。

## 検証範囲

- 以前のテストはTransformの引数を無条件に受け入れていたため、この型違いを検出できませんでした。UEログと同じRotator必須の契約をテストに追加し、修正前に3件が同じ型違いで失敗することを再現しました。
- 修正後は基本姿勢・16歩行フレームで回転／平行移動／非等方スケールを変えたケースも検証します。座標変換したQuatをUE変換へ渡すことを確認し、UEのEuler変換自体は模擬実装しません。
- Pythonのbridge 108件、native起動・取り込み38件を検証。更新ZIPのCRCと収録ソースの一致を確認します。
- Windows UE 5.8.3での修正後の取り込み完了・描画は未確認です。次の停止がある場合は新しい `Saved\Logs\NativeImport-日時-識別子.log` で判定します。

# UE単独プレイ0.13.0 更新手順

[UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.13.0/downloads/UEBridge-update-0.13.0.zip) / [MOD 0.13.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.13.0/downloads/minecraft-ue-bridge-0.13.0.jar) / [一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.13.0/downloads/UE-Minecraft-MVP-0.13.0.zip)

取り込みの追加停止原因を修正し、オフハンドの所持・交換・表示・保存、原作に合わせた段差・到達距離・空の表示を追加しました。0.12.1～0.12.4の修正も含みます。既存のMOD 0.12.0の書き出しとUE保存を継続して使えます。Windows UE 5.8.3のC++ビルド成功は利用者の0.12.1ログで確認済みですが、**0.13.0のUEコンパイル・取り込み完了・描画・ゲーム操作は実機未確認**です。検証内容は [AUDIT_0.13.0.md](AUDIT_0.13.0.md) に記録しています。

## 使用中のUEプロジェクトへ適用

1. UE EditorとUEゲームを終了します。
2. 次の使用中フォルダー全体を、別の場所へコピーしてバックアップします。これにはContent、Config、Savedと以前のSourceが含まれます。

   ```text
   C:\Users\tsush\Downloads\UE-Minecraft-MVP-0.2.0\UE-MINECRAFT\unreal\UEBridge
   ```

3. **UE更新ZIP**を一時フォルダーへ展開します。ZIP直下の `Source` フォルダーと、すべての `.cmd`・`.ps1`・`.py` を、上記の `UEBridge.uproject` があるフォルダーへコピーして上書きします。Sourceはフォルダーごと統合コピーします。以前の2ファイルだけの交換では今回の機能は入りません。
4. そのフォルダーの **Play-Native.cmd** をダブルクリックします。初回はC++を再ビルドし、取り込みスクリプト更新を検出して専用nativeマップを再作成します。自動処理中のUE画面は閉じずに待ちます。ファイル選択が表示されたら、以前と同じ書き出しの `native_manifest.json` を選んでください。
5. 次回は同じPlay-Native.cmdでUEの保存から再開します。更新後に素材が不足しているなどのエラーが出た場合は、出力全文または表示された `Saved\Logs\NativeImport-日時-識別子.log` を保持してください。

更新ZIPにContent、Config、Saved、uproject、ビルド済みDLLは含みません。既存レベル `/Game/UE`、Minecraftの元ワールド、以前の配布物を削除しません。再取り込みはUE保存の初期化とは別の操作で、同じ書き出しIDの `Saved\NativeWorlds` を使用します。失敗した取り込みを成功と記録せず、次回も再試行します。

EngineRootの指定が必要な場合は、WindowsのPowerShellで次の2行を実行します。

```powershell
Set-Location -LiteralPath 'C:\Users\tsush\Downloads\UE-Minecraft-MVP-0.2.0\UE-MINECRAFT\unreal\UEBridge'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Play-Native.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8'
```

## 太陽・月の画像を新しく書き出す場合

Minecraft 1.21.11は画像を `environment/celestial` へ移し、月を8枚に分割しました。旧MOD 0.12.0は旧パスしか取得しておらず、通常の1.21.11の書き出しに天体画像がありません。UE側の数式だけを修正しても、この不足分は表示できません。

MOD 0.13.0は現在選択中のリソースパックから新旧パスの画像を取得し、書き出しのサイズとSHA-256へ含めます。画像自体は配布ZIPへ同梱しません。

1. 専用Minecraftを終了します。
2. そのインスタンスの `mods` にある旧 `minecraft-ue-bridge-0.12.0.jar` を、modsの外のバックアップ先へ移します。削除せず保管し、新旧のBridge MODを同時に読み込ませません。
3. `minecraft-ue-bridge-0.13.0.jar` をmodsへ入れ、同じMinecraft 1.21.11・Fabric環境で起動します。
4. 同じ元ワールドと使いたいリソースパックで `/uebridge native export` を実行し、完了後にMinecraftを終了します。
5. UEBridgeフォルダーのPowerShellで `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Play-Native.ps1 -Reimport` を実行し、**新しく作った** `native_manifest.json` を選びます。

新しい書き出しは別IDのUE保存になります。以前のUE編集を新しい書き出しへ自動合成する機能はありません。前のUE編集を再開する場合は、その前の `native_manifest.json` を `-Manifest` で指定してください。両方の書き出しと保存を保持します。

## 追加された操作と範囲

- **F／設定済み交換キー**：主手とオフハンドを交換。インベントリに追加されたオフハンド枠は左／右・Shiftクリックと数字キーの交換にも対応します。主手設定が左ならHUDと持ち手も左右へ反映します。
- オフハンドのアイテムID・数量を取り込み、UEワールドと同時に保存します。旧保存にオフハンドがない場合は空として読み込みます。弓はオフハンドの矢から優先消費します。
- 原作の段差高さ0.6ブロック、サバイバルのブロック操作4.5ブロック・近接攻撃3ブロック、クリエイティブ5ブロックに合わせます。選択枠も同じブロック距離を使います。代表的な破壊不能ブロックをサバイバルで即時破壊しません。
- ポーズ中にボタンの解除時間が進まないよう修正。押されたボタンの読み込み時は石20tick・木30tickの通常時間から再開します。元の残りtickは書き出しにありません。
- UE単独モードの矢の衝突をモブのダメージへ接続。基本ダメージは発射時の引き具合から計算します。原作の衝突時速度・クリティカル・エンチャント・矢の保存を含む完全な弓仕様ではありません。
- 空は書き出し時点のスナップショットです。1.21.11の太陽・月の位置、大きさ、月相と雨のフェードを反映し、ネザー・エンドでは天体を表示しません。Minecraft光マップ方式はEscの照明OFFで確認します。照明ONはUEの大気表現です。雲・星・エンド専用背景と昼夜／天候の継続シミュレーションは未対応です。

盾の防御、オフハンドからの右クリック使用、耐久値・エンチャントなどの全データ保持、クラフト、空腹・経験値・防具、流体、全モブAI・戦利品、レッドストーン、爆発耐性と連鎖TNTは未完成です。移動物理もUEのカプセルを使う近似が残ります。

## 起動後の確認

まず取り込みが最後まで進み、Minecraftを終了した状態で専用UEゲームが開くことを確認します。次にFで両手を交換してE画面・HUD・一／三人称を確認し、保存・再開して数量が残ることを確認します。別ウィンドウやEscから戻って視点が跳ねないこと、ボタンがポーズ中に解除されないこと、矢がモブへダメージを与えることも確認してください。

空の確認は画像を含む新しい書き出しと照明OFFで行います。ここに書いた確認項目は実機での合格報告ではありません。UEの自動テスト名とクラウドで実行した検証は監査記録へ記載しています。

# UE単独プレイ0.14.0 更新手順

報告された14項目の実装修正をまとめた版です。Javaのビルド・自動テスト、取り込み処理の回帰テスト、UEから独立したC++計算を確認しています。**Windows UE 5.8.3での今回のC++コンパイル、実描画、クリック・IME・物理の統合動作は未確認です。** 修正内容と確認範囲は [AUDIT_0.14.0.md](AUDIT_0.14.0.md) に記録しています。

[UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.14.0/downloads/UEBridge-update-0.14.0.zip) / [MOD 0.14.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.14.0/downloads/minecraft-ue-bridge-0.14.0.jar) / [一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.14.0/downloads/UE-Minecraft-MVP-0.14.0.zip)

## 適用

1. MinecraftとUEを終了します。使用中のUEBridgeフォルダーとMinecraftの専用インスタンスをコピーして保管します。
2. 専用Minecraftの `mods` にある旧 `minecraft-ue-bridge-*.jar` を別の場所へ移し、MOD 0.14.0だけを入れます。Minecraft 1.21.11・Fabric・Java 21は継続できます。
3. Minecraftで元のワールドを開き、反映したい時刻・リソースパックで `/uebridge native export` を実行します。完了後に表示される新しい `native_manifest.json` の場所を控え、Minecraftを終了します。羊毛、GUIアイコンの陰影、全武器の攻撃属性には新しい書き出しが必要です。
4. UE更新ZIPを一時フォルダーへ展開します。直下の `Source` とすべての `.cmd`・`.ps1`・`.py` を、次の使用中のフォルダーへ統合コピーして上書きします。

   ```text
   C:\Users\tsush\Downloads\UE-Minecraft-MVP-0.2.0\UE-MINECRAFT\unreal\UEBridge
   ```

5. 初回はPowerShellで次を実行し、入力欄へ手順3の新しい書き出しの絶対パスを貼り付けます。C++の再ビルドと材質・UI・モブの再取り込みが必要です。自動処理中のUE画面は閉じずに待ちます。

   ```powershell
   Set-Location -LiteralPath 'C:\Users\tsush\Downloads\UE-Minecraft-MVP-0.2.0\UE-MINECRAFT\unreal\UEBridge'
   $manifest = (Read-Host '新しいnative_manifest.jsonの絶対パスを貼り付け').Trim('"')
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Play-Native.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8' -Manifest $manifest -Rebuild -Reimport
   ```

6. 次回から同じ `Play-Native.cmd` で再開します。

更新ZIPにContent・Config・Saved・uproject・ビルド済みDLLは含みません。既存マップや保存、以前の配布物を削除しません。**新しい書き出しは新しいワールドIDとなり、UE側でも別の保存を使います。以前のUEで設置・破壊した結果を、新書き出しへ自動移植する機能はありません。** 旧書き出しを選べば以前のUE保存を継続できますが、欠けている羊毛レイヤーと新しいアイコンは復元できません。

## 操作と確認

- Escのメニューで感度を5%ずつ調整できます。設定は `Saved\Config\NativeControls.ini` に保存します。
- F5（Minecraft側で変更した場合はその割り当て）で一人称→後方三人称→前方三人称。Escの「視点を切り替える」でも変更できます。
- Eでクリエイティブ検索を開き、右上の「所持品へ」で36枠＋オフハンドの管理へ切り替えます。「検索へ」で戻ります。左クリック移動、右クリック分割・1個配置、Shift移動、数字キー交換、Fでオフハンド交換。
- 飛行はSpaceの二度押し。方向入力を離しても慣性が残ります。横方向と上下方向を独立して減衰させます。
- 敵への攻撃はクリックごとです。押し続ける操作はブロック採掘を継続します。剣・斧などの回復時間は異なり、未回復の攻撃は威力が下がります。照準下に回復バーを表示します。
- 被ダメージ時に赤色を重ね、最初の10tickは同等以下の攻撃を拒否します。より強い攻撃は差分だけ加算します。死亡後は地面に合わせて倒れ、20tick後に消えます。
- 照明ON/OFFとも書き出した太陽・月・明るさを使います。ONはUEの動的な影を追加します。OFFは原作の光マップ方式です。**時刻は書き出し時点のスナップショットで、停止中のMinecraftとライブ同期したり、UE側だけ日周期を進めたりはしません。**
- 自動保存でゲーム全体をポーズしません。保存開始時にデータをコピーし、分割して書き込みます。大きなワールドのコピーやディスクの同期処理による短い処理時間は実機測定が必要です。

クラフト・防具の装備UI、敵AIの全経路探索、エンチャント、特殊武器の全効果などは今回の14項目とは別の未対応範囲です。

## エラーの場合

`Saved\Logs\NativeImport-日時-識別子.log` が取り込みログ、`Saved\Logs\UEBridge.log` がゲームのログです。ビルドエラーの場合はBuild-UEBridgeの出力全文を保持してください。今回のバージョンはログへ `Bridge 0.14.0` と出力します。

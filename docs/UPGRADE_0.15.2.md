# UEBridge 0.15.2 更新手順

0.15.0/0.15.1からの描画・入力・戦闘修正です。既存プロジェクトを更新できます。

## 適用

1. MinecraftとUnreal Editorを終了します。
2. modsの旧UEBridge JARを削除し、minecraft-ue-bridge-0.15.2.jarへ交換します。Fabric/Fabric APIはそのまま使えます。
3. Minecraftの対象ワールドで `/uebridge native export` を実行し、新しいnative_manifest.jsonの絶対パスを控えます。GUIアイコンの照明修正は新しい書き出しが必要です。
4. UEBridge-update-0.15.2.zipを展開し、内容を現在のUEBridgeフォルダーへ上書きします。SourceとPythonヘルパーを両方更新してください。このZIPにContent/Config/Savedは含みません。
5. UEBridgeフォルダーでPowerShellを開き、以下を実行します。JSONは手順3の新しいパスに変更してください。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Play-Native.ps1" -Manifest "C:\UEBridgeTest\MC-Test\uebridge-export\新しい出力フォルダー\native_manifest.json" -Rebuild -Reimport
```

-RebuildでC++を更新し、-Reimportで生成マテリアルを照明リビジョン5へ更新します。今回起動するPowerShellプロセスだけ実行ポリシーを変更します。
一式ZIPを使う場合は別フォルダーへ展開し、同じ新しいJSONをPlay-Native.cmdで選択できます。
新しい書き出しは別パッケージの初期状態になります。以前のUEプレイ中に編集したブロック・インベントリ・モブ状態は自動で新しいパッケージへ移りません。以前のSavedと書き出しを残してください。

## 修正内容

- 地形・手持ち・ドロップアイテム・モブ・スキンの三角形の並びをUEの表面判定に合わせ、外向きの法線は維持。
- GUIアイコンの照明に本家の外側のY反転を反映。一人称の手持ちアイテムをバニラ照明で描画し、照明をカメラに固定して視点方向による陰影変化を抑制。
- ノックバックをCharacterMovementのLaunchCharacter経由で適用し、直後0.25秒のAI移動による上書きを抑制。
- 死亡時は重力と衝突を維持し、着地後の回転モデルの最下面を足元へ合わせる。
- マウス相対移動量をPlayerInputの補正前に受け取り、UpdateRotationでカメラ評価前に反映。フォーカス復帰・メニュー操作時は蓄積を破棄。
- モブの有効な経由点を維持し、停止が続いた時だけ再探索。胴体の旋回を秒単位で制限し、頭の角度を補間。
- ブロックアウトラインを黒の40%アルファへ変更。表面側だけ描画して裏面との二重合成を抑制。
- 文字の影を表示RGBの1/4へ暗くし、字形の描画位置を画面ピクセルへ揃える。

Windows UE 5.8.3の実コンパイル・GPU描画・マウス入力・実戦闘はクラウドでは実行できていません。修正の実機での効果は確認が必要です。種別固有のモブ行動すべてがバニラと同一になったという変更ではありません。

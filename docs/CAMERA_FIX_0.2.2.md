# UEカメラの左右修正 / 数FPSの切り分け

Minecraft MOD 0.2.1はそのまま使用できます。変更対象はUEのC++ソースだけです。
UE更新番号0.2.2はMODのバージョン番号と独立しています。

## 数FPSに見える場合、先に設定を確認

1. UEで「編集 → エディタの環境設定」を開く。
2. 検索欄で `background` / `バックグラウンド` を探す。
3. **Use Less CPU when in Background** をOFFにする。
4. UEとMinecraftを同時表示し、Minecraftを操作している間のUE FPSを確認する。
5. UE側のコンソールで `stat fps` を実行すると実フレームレートが見える。

この設定はPCのエディタ設定です。配布ソースから勝手に変更しません。
低FPSの原因として考えられますが、実機での改善はまだ確認していません。
OFFでも数FPSなら、UEのFPS、MCのF3に表示されるFPS、`/uebridge status` の結果を確認します。
送信頻度設定が120Hzでも、実際のゲーム描画FPS以上には更新できません。

## 左右修正パッチを適用

1. 現在のUEレベルを保存し、UE EditorとVisual Studioを閉じる。
2. `UEBridge-camera-fix-0.2.2.zip` を別の空フォルダへ展開する。
3. 中の `Source` フォルダを、使用中の `unreal/UEBridge/` にコピーし、同名ソースを置き換える。
   `Content`、`Config`、`Saved` は置き換えない。作成したレベル/アセットは保持する。
4. `UEBridge.uproject` を右クリックし **Generate Visual Studio project files** を実行。
   Windowsでは必要に応じて「その他のオプションを確認」から表示される。
5. 生成された `UEBridge.sln` をVisual Studioで開く。
6. 構成を **Development Editor / Win64** にし、「ビルド → ソリューションのビルド」を実行。
7. ビルド成功後、uprojectを開き、準備済みのテストレベルで通常Playを開始。
8. Minecraftで `/uebridge recenter` を実行。2秒ほど待って視点と左右移動を確認。

既存のBinariesがあるため、ソースをコピーしてuprojectを開くだけでは古いDLLが使われる
ことがあります。手順6の再ビルドが必要です。独自にC++を編集している場合は先に変更を保存してください。

## 修正内容

- MC yawの正方向に対してUE yawの符号を反転していたため、マウス左右が逆になっていた。
- UE座標を `100 * (mcZ, -mcX, mcY)` に統一し、Yaw=`mcYaw`、Pitch=`-mcPitch` とする。
- カメラ、Character、TNT、周辺プレビュー、矢の向きで同じ座標基準を使用する。
- 方向とカメラ回転の一致を検証するUE Automationテストを追加。

UE Editorがないクラウドでは、このC++変更のUEビルド/Automationテストは未実施です。
原因をコード上で確認して修正していますが、実機で再ビルド後の確認が必要です。

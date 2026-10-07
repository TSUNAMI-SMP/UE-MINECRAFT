# UEビルド修正パッチ0.11.1

[修正パッチZIPをダウンロード](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/UEBridge-build-fix-0.11.1.zip)

ユーザー実機のUE5.8ビルドログで判明した、0.11.0のソースのコンパイルエラーを修正します。MODは **0.11.0のまま** 使用します。元の0.11.0配布ZIPは保持し、その上にこのパッチを適用します。GitHubのこのブランチのソースには修正を含みます。

## 修正内容

- `BridgeBlockPreview.cpp`：ローカルのRoleをShapeRoleへ変更し、AActor::Roleとの衝突を解消。
- `BridgeWorld.cpp`：Owner／CenterをBlockOwner／ShapeCenterへ変更し、AActorおよび地形クラスのメンバーとの衝突を解消。ラムダ内のOwnerも変更。
- `BridgeItemWorld.cpp`：TObjectPtr配列を生のポインターとしてauto推論していた5か所を、明示したABridgeDroppedItem*で反復。
- `BridgeVideo.cpp`：3か所のENQUEUE_RENDER_COMMANDを条件式の波括弧内へ配置。UE5.8のマクロ内で宣言されるTSTR型が、後続のキュー登録から参照できるスコープを確保。
- `BridgeBlockGeometry.cpp`：モデル座標From／Toを初期化し、C4701警告へ対処。

`IncludeOrderVersion`のUpgrade案内は今回のビルド失敗の原因ではありません。

## 上書きとビルド

1. UEを閉じます。Play停止だけでなく、Editor自体を終了します。
2. ZIPを一時フォルダーへ展開します。
3. 中の `Source/UEBridge/` にある次の **5ファイルだけ** を、使用中プロジェクトの同じ場所へコピーし、同名ファイルを上書きします。

   ```text
   C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/Source/UEBridge/

   BridgeBlockGeometry.cpp
   BridgeBlockPreview.cpp
   BridgeItemWorld.cpp
   BridgeVideo.cpp
   BridgeWorld.cpp
   ```

4. 使用中の `UEBridge/Build-UEBridge.cmd` を実行し、UEのインストール先に `C:\Program Files\Epic Games\UE_5.8` を入力します。
5. 最後に `Result: Succeeded` を確認してから、使用中のUEBridge.uprojectを開きます。初回0.11.0導入途中なら、[素材の書き出し・取り込みと起動手順](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/blob/ue-bridge-0.11.0/docs/UPGRADE_0.11.0.md)の続きへ進みます。

Source全体、Content／Config／Saved、保存済みレベル、取り込み済み素材は削除しません。MOD・GPU DLL・Pythonスクリプトの交換はこのパッチでは不要です。今回のコンパイル修正だけのために素材を書き出し直す必要もありません。

失敗が続く場合は、最初のerrorから最後のResultまでのビルドログを添付してください。

## 検証範囲

添付ログ内の全コンパイルエラーと座標初期化警告を修正し、関連する独立C++の地形・映像タイミング計算、Python65件を再確認しました。クラウドにUE5.8はないため、修正後のUEモジュールのビルド・実機描画成功は未確認です。

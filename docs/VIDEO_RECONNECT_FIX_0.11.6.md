# 映像再接続修正 0.11.6

UEの描画が続いているのにMinecraft側が `UE映像待ち: EOFException` になる場合の累積修正です。
GPU共有（UEB5）のTCP接続がUE側で閉じられたとき、MinecraftがGPU共有を再試行し続けず、次の接続をJPEG（UEB3）へ自動的に切り替えます。UEの再起動やワールドの作り直しは不要です。

## ダウンロード

[映像再接続修正済みMOD JAR](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/minecraft-ue-bridge-0.11.0-video-reconnect-fix.jar)
（SHA-256: `099bf72ee99910212f4e9d3430c68a80486836bea7fca1ac0f1668813f6939a8`）

## 差し替え手順

1. MinecraftとUEを終了します。
2. `C:/UEBridgeTest/MC-Test/mods/` にある既存の `minecraft-ue-bridge-*.jar` をバックアップフォルダーへ移します。Fabric APIは移動しません。
3. 上のJARを同じ `mods` フォルダーへ置きます。Bridge MODはこのJAR **1個だけ** にしてください。
4. UEをPlayで起動し、`/uebridge import start` → `READY` → `/uebridge control ue` の順で再接続します。

これはMODだけの修正です。UEのSource、Content、Config、Saved、保存済みレベル、素材の再ビルド・再取り込みは必要ありません。既存の0.11.0 JARは削除せず別フォルダーに保管できます。

### すぐに確認する場合

新JARへ差し替える前でも、Minecraftで次を実行するとJPEG経路を明示できます。

```text
/uebridge video transport jpeg
```

その後、UEをPlayにして `/uebridge import start` を実行します。JPEGで映像が出る場合は、原因がGPU共有経路の切断であることが確認できます。新JARでは `auto` のままでもGPU共有のEOFを検出してJPEGへ降格します。

クラウド環境にはUE 5.8・Windows GUI・RTX 5060がないため、実機の映像・FPS・GPU共有は未確認です。

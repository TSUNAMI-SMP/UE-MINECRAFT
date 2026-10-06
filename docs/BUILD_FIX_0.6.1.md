# UE 0.6.0のC4458ビルド修正

[修正パッチZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-build-fix-0.6.1.zip)

BridgeCharacter.cppのラムダ引数MeshがACharacter::Meshを、BridgeWorld.cppのローカル変数OwnerがAActor::Ownerを隠していました。
UE5.8のWindowsビルドでC4458となり、コンパイルが止まる原因です。変数名をVisualMeshとSourceVoxelへ変更します。
警告を無効にする設定変更は行いません。IncludeOrderVersionの[Upgrade]表示は今回の停止原因ではありません。

## 適用

1. UEとVisual Studioを閉じる。
2. ZIPを別の場所へ展開する。
3. 中のSource/UEBridgeにある**BridgeCharacter.cppとBridgeWorld.cppの2ファイルだけ**を、使用中プロジェクトのSource/UEBridgeへ上書きする。
   元のSourceフォルダ全体は削除しない。Source/Sourceという二重の配置にしない。
4. いつものUEBridge.uprojectの横にあるBuild-UEBridge.cmdを実行する。
5. BUILD SUCCESSFUL後、その同じuprojectを開き、保存済みレベルを読み込む。

今回のログのコピー先は次の場所です。

```text
C:/Users/tsush/Downloads/UE-Minecraft-MVP-0.2.0/UE-MINECRAFT/unreal/UEBridge/Source/UEBridge/
```

MODは0.6.0のまま。レベル、テクスチャ、Niagara設定、Content/Config/Savedの変更は不要です。
UE起動後はPlayを開始し、[0.6.0の手順](UPGRADE_0.6.0.md#起動)どおりimport start→READY→control ueを行う。
通信仕様とstatusのUEバージョン表示は0.6.0のままです。

## 確認範囲

実機のビルドログで特定された3箇所を修正。差分が名前の変更のみであることと、配布ZIPの内容・CRC・SHA256を確認。
クラウドにはUE5.8とWindowsコンパイラがないため、修正後のUEビルド・描画は未実行です。
元の0.6.0配布ZIPは保持しているため、そちらを使用する場合も、このパッチを後から適用してください。

# ダウンロード

## 起動修正版 0.2.1（こちらを使用してください）

[修正版MODのJARをダウンロード](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.2.1.jar)

Minecraftを終了し、modsから旧Bridge MOD（0.1.0/0.2.0）だけを取り出し、このJARへ差し替えてください。Fabric APIは残します。Minecraft 1.21.11・Java 21・Fabric Loader/APIの変更、UEソースの更新は不要です。

[ソースとMODの一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UE-Minecraft-MVP-0.2.1.zip)

原因: Mixin専用パッケージが通常の初期化クラスまで含んでいたためFabricがクラス読み込みを拒否。Mixinだけを専用サブパッケージへ移動し、回帰テストを追加しました。ビルドとJava 17件のテスト成功。GUI起動はPCで再確認が必要です。

旧版アーカイブにはこの起動不具合があります。今後の導入には0.2.1を使ってください。

## 改良版 0.2.0

[UE-Minecraft-MVP-0.2.0.zip をダウンロード](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UE-Minecraft-MVP-0.2.0.zip)

ビルド済みFabric 1.21.11 MOD・UEソース・設定手順を含みます。
接続診断、TNT判定改善、周辺ブロックの手動プレビュー、任意の弓試作を追加。
[更新手順と未検証項目](../docs/UPGRADE_0.2.0.md)を確認し、MC MODとUEを両方更新してください。
UEのVFX/破壊壁アセットは未作成で、UE 5.8 C++ビルドと実機動作は未検証です。
ZIPのチェックサムは `UE-Minecraft-MVP-0.2.0.zip.sha256` にあります。

保存できない場合はZIPのGitHubページを開き、右上の **Download raw file**（下向き矢印）を押してください。
非公開リポジトリの場合はGitHubへログインします。

## 旧版 0.1.0（既存リンクは維持）

[UE-Minecraft-MVP-source-and-mod.zip](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UE-Minecraft-MVP-source-and-mod.zip)

最初に提供したZIPです。導入中のファイルは差し替えていません。
SHA-256: `24e07cf382477ca0281175322767ff1cbad98a2b609b402f0c4475c9ba9feed0`

どちらもMODを使うには対応するFabric LoaderとFabric APIが別途必要です。

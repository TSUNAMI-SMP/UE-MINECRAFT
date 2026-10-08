# 検証記録（2026-10-08）

- Minecraft Java 1.21.11、Yarn 1.21.11+build.6、Fabric Loom 1.14.10、Gradle 9.2.1、Java 21で `genSources` と `stageReferenceInputs` が成功。
- 共通4,660クラス・クライアント専用1,962クラス、合計6,622 Javaソースを実取得。調査対象と追跡した依存5,975ソース、バニラ資源687ファイルを収集。
- 全7分野の候補パスを実際のソースへ照合。未一致の候補0件。
- 公式バージョン情報によるクライアントJARのSHA-1照合、入力と各ソース・資源のSHA-256記録。全6,622ソース・687資源のハッシュを再照合。
- 草ブロックモデルの側面下地4面とオーバーレイ4面、上面・オーバーレイのtint指定を実データで確認。生成された選択資料ZIPのCRCと全収録内容を照合。
- 収集処理の7テストが成功。依存の追跡、親モデル・画像・mcmeta収集、結果ZIP、別バージョン／不一致ハッシュの拒否、パス検証、結果上書き防止。
- PowerShell 7.4/Linuxから `Get-Minecraft-Code.ps1` でJDK/Python検出、Gradle実行、資料収集、成功表示まで通して確認。
- Gradle Wrapper JARと起動スクリプトは公式Gradle v9.2.1のファイル。JARのSHA-256をGradleが公開するチェックサムと照合：`423cb469ccc0ecc31f0e4e1c309976198ccb734cdcbb7029d4bda0f18f57e8d9`。Gradle本体のZIPもwrapper設定の公式SHA-256で照合。

Windows PowerShell 5.1での実行とユーザー端末のJDK自動検出は未実機確認です。PowerShellスクリプトは5.1で利用できる構文を使用しています。Windowsでは `.cmd` から同スクリプトを起動します。

取得結果はYarn名の逆コンパイル資料で、Minecraftの開発用原本ではありません。UEの修正・実画面検証はこの取得ツールの検証には含みません。外部sounds.json・OGG、追加リソースパック、実プレイのバイオーム値は未収集です。

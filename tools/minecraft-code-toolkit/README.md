# Minecraft 1.21.11 コード取得ツール

Minecraft Java版1.21.11のコードを、Fabric Loom 1.14.10・Yarn 1.21.11+build.6・Loom付属Vineflowerで逆コンパイルして整理します。MODをゲームへ入れる必要はありません。このツールはゲームの起動、Minecraft・UEの保存やMODの変更、UE向けコード変換を行いません。

## 実行

1. ZIPを `C:\UEBridgeTest\Minecraft-Code-Toolkit` などの独立したフォルダーへ展開します。ZIPの中から直接実行しないでください。
2. **Java 21のJDK**が必要です。ゲーム用のJREだけでは不足します。未導入の場合は [Microsoft Build of OpenJDK](https://learn.microsoft.com/java/openjdk/download) などでJDK 21を用意します。
3. `Get-Minecraft-Code.cmd` をダブルクリックします。初回はインターネット接続が必要で、Gradle・Minecraft・マッピング・ライブラリを取得します。数GBの空き容量を確保してください。管理者実行は不要です。
4. `SUCCESS:` が出るまで待ちます。初回の逆コンパイルは数分以上かかることがあります。完了後の `results\Minecraft-Reference-1.21.11-日時-識別子` が結果です。

PythonはUE 5.8付属のものを優先して自動検出します。Python 3.10以降なら別のインストールも利用でき、追加パッケージは不要です。

JDKが自動検出できない場合は、そのフォルダーを指定します。

```powershell
Set-Location -LiteralPath 'C:\UEBridgeTest\Minecraft-Code-Toolkit'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Get-Minecraft-Code.ps1 -JavaHome 'C:\Program Files\Microsoft\jdk-21.x.x'
```

`jdk-21.x.x` は実際のフォルダー名に置き換えます。UEが別の場所にある場合は `-EngineRoot '実際のUE_5.8の場所'`、Pythonを直接指定する場合は `-PythonExe 'python.exeの絶対パス'` を付けます。

## 結果

| ファイル・フォルダー | 内容 |
| --- | --- |
| `sources/all` | クライアント・共通処理の全Javaソース。呼び出し元や追加の依存を検索するために使用 |
| `sources/selected` | プレイヤー操作、戦闘、UI、ブロック描画、空、手持ち、モブAIと、追跡できたimport・同一パッケージ依存 |
| `README.md` | 分野別の入口と、草ブロックの調査手順 |
| `entrypoints.json` | 調査語に一致したソース位置・行番号・行の内容。実装・参照・コメントが混在するため、メソッド宣言だけの一覧ではない |
| `source-index.json` | 元JAR、各ファイルのSHA-256、調査分野 |
| `vanilla` | バニラの草ブロックのモデル・親モデル・画像・カラーマップ、GUI、空、シェーダー。追加リソースパックは含まない |
| `asset-index.json` | 収集した資源のパス・SHA-256 |
| `mappings.tiny` / `version.json` | 名前対応と公式バージョン情報 |
| `external-assets.json` | ゲームJARの外部にある音定義・OGG用の公式asset index情報。外部音声ファイル自体はこのツールでは取得しない |
| `report.json` | 版、件数、入力のハッシュ、確認範囲 |
| `unmatched-patterns.json` | この版に存在しなかった候補パス。取りこぼし調査に使用 |
| `Minecraft-Reference-selected.zip` | 全ソースの重複コピーを除いた、調査用コード・資源・索引のまとめ |

結果ZIPは手元で生成されます。この配布ツールにMinecraftのソース・テクスチャ・ゲームJARは含みません。取得先はLoomが利用する公式Minecraft配布・Fabric・Gradle/Mavenです。クライアントJARは公式バージョン情報のSHA-1と照合し、資料にはSHA-256も記録します。

逆コンパイル結果は、バイナリから復元したコードです。Mojangの開発用原本・コメントではありません。名前はYarnによるもので、別のマッピングの解説記事とは名前が異なる場合があります。依存ライブラリの実装・実プレイの値・MODや追加パックの変更までは取得しません。サーバー側の戦闘・AIとクライアント側の描画・演出を区別し、関連する呼び出し順も調べてからUE向けに実装してください。

## 草ブロックの黒い土・白い草

バニラの `grass_block.json` は、側面の下地と草オーバーレイを同じ位置へ重ねます。上面と草オーバーレイに `tintindex=0` があり、土を含む下地にはありません。

そのため、修正前に以下を照合します。

- `#side` と `#overlay` の両方が取り込まれ、同一位置の面として削除されていないか。
- オーバーレイの透明部分から下地が見えるか。色だけを黒背景と合成していないか。
- 設置時の上面・オーバーレイへ草色が渡るか。土へ同じ色を適用していないか。
- `BlockColors`、バイオーム色、光マップ、描画レイヤーの処理が一致するか。

資料収集はこの症状の修正そのものではありません。現行UE側のモデル・材質・設置処理との比較が必要です。

## エラー時

`.work\Get-Code-日時-識別子.log` に実行ログが残ります。失敗した場合はこのログを確認します。既存の結果は上書きせず、新しい実行ごとに別フォルダーを使います。

ソース生成が既に成功していて、資料の整理だけを再実行する場合は `Get-Minecraft-Code.ps1 -SkipGeneration` を利用できます。空の入力や別バージョンを成功扱いにはしません。

この配布物のスクリプト・設定はUE-MINECRAFTプロジェクトの独自ツールです。Gradle Wrapperのライセンスは `GRADLE-WRAPPER-LICENSE.txt` を参照してください。

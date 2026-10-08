# 0.15.4 検証記録

2026-10-08。提供された診断ZIPはCRC正常、532エントリー、クラス481/481取得、記録200/200ティック、完了理由completeです。欠落リソースはairのmissingno参照だけです。添付クラスはローカルでjavap解析し、解析対象のクラスファイルや利用者の座標・ID・画像をリポジトリや配布物へ追加していません。

`LivingEntity.takeKnockback` の抵抗補正、水平速度の1/2、地上時の上限0.4を確認。ハスクの上向き速度0.313600006ブロック/ティックを参照し、0.4から重力0.08を引き0.98を掛けた値との一致を独立計算で確認しました。これはUEの実際の衝突・重力更新の検証ではありません。LookAroundGoalの開始確率0.02、有限時間の視線制御も参照しています。

検証:

- Java 21 / Fabric Loomのtest build成功。JUnit207件、失敗0。
- bridge/tests Python114件成功。アイテムの両面フラグの保存と不正型拒否も既存ケースへ追加。
- native UI Python20件成功。native setup Python20件中19件成功、環境依存1件スキップ。
- production mesh math：128種のコライダー往復、6面の向き・重なり、反射後の法線による表裏選択が成功。
- production mob AI、combat/movement、outline8チェック成功。
- native regressions：真下へ連続入力した際の角度保持、反射面・一枚の線、ノックバックの連続合成、記録された原作の上向き速度との数学的比較が成功。
- Python編集ファイルの構文、git diff --check、配布JAR/ZIPのCRC・作業ファイルの一致・SHA-256を確認。

native setupの最初の起動ではtoolsへのPython検索パスが不足しました。PYTHONPATH=toolsで再実行して上記結果を確認しています。

未実行: Windows UE 5.8.3のC++コンパイル、ShaderCompileWorker、実GPUでの透明な線・欠けた面・手持ちのチカチカ、実マウス、実際のノックバック・死亡時の衝突、実FPS。数学テストはこれらの代替ではありません。

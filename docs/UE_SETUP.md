# UE 5.8 デモ設定（実機で実施）

クラウドにUE Editorがないため、この手順とC++コードはUE 5.8実機では未検証です。
エディタの表示名が違う場合は対応項目を確認してください。アセットはこの新規
`unreal/UEBridge/Content/` 内に作成し、他のプロジェクトから移動・変更しないでください。

## C++プロジェクト

`UEBridge.uproject` を開きます。エンジン選択を求められたらインストール済みのUE 5.8を
指定。モジュールのコンパイルを許可します。必要なら右クリックからプロジェクトファイルを
生成し、IDEで `UEBridgeEditor / Development Editor / Win64` をビルドしてから開きます。

NiagaraとGeometry Collectionプラグインを有効にしています。独自エンジンビルドで
EngineAssociationが一致しない場合は、この新規プロジェクトだけをそのエンジンへ関連付けます。

Linuxにエンジンが既にある場合のビルド例（`UE_ROOT` は自分のインストール先）:

```sh
"$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" UEBridgeEditor Linux Development "/absolute/path/UE-MINECRAFT/unreal/UEBridge/UEBridge.uproject"
```

## レベルとカメラ

1. 新規のBasicレベルを作り、`Content/Maps/BridgeDemo` として保存。
2. Project Settings → Maps & ModesでEditor Startup Map/Game Default Mapをこのレベルへ。
3. World SettingsのGameMode Overrideを `BridgeGameMode` にする。
4. PlayerStartを床から約100cm上に配置し、Yaw=0にする。
5. Place ActorsからC++ `BridgeReceiver` を **1個だけ** 置く。
   Target Characterは空でよいです。BeginPlay時にプレイヤーCharacterを自動取得します。
6. Playは1プレイヤーの通常Playを使う（Simulateや複数PIEウィンドウは使わない）。

`BridgeGameMode` が `BridgeCharacter` を生成し、そのCameraを利用します。
MCの目線高さ162cm相当。UEのCharacterMovementは停止し、MCの位置を受信して反映。
カメラだけのデバッグでUEへ別の入力を与える必要はありません。

## Niagara爆発

1. `Content/VFX/NS_BridgeExplosion` としてNiagara Systemを新規作成。
2. エンジンにある一度だけ発生するSprite Burst系テンプレートを利用するか、
   空SystemにSprite emitterを追加。短時間（例0.5秒）で終了する設定にする。
3. Burst=100程度、橙色→暗色、短いLifetime、中心から外向きVelocityを設定。
4. Loopを無限にせず、一度のバーストで完了するようにする。
5. レベルのBridgeReceiverの **Explosion System** にこのSystemを割り当てる。
6. エディタ上のNiagaraプレビューで目に見える爆発が出ることを確認。

Niagaraの描画とChaosの破壊は同じ受信イベントから別々に実行します。
NiagaraアセットなしでもイベントはACKされるので、ACKだけでVFX成功としないでください。

## Chaosの簡単な壁

1. UE標準Cubeで壁を作る。幅400cm・厚さ25cm・高さ300cm程度。
   UE座標でPlayerStartの約350cm前（+X）に配置し、壁の中心が床から150cmになるよう調整。
2. Fracture ModeでそのCubeから **Geometry Collection** を作成し、
   `Content/Physics/GC_BridgeWall` に保存。普通のStaticMeshのままでは壊れません。
3. Uniform Voronoi等で20〜40片にFractureして、破片が生成されたことを確認。
4. CollectionをCluster化して、初期状態が壁の形を維持するようにする。
5. レベル上のGeometryCollection ActorでSimulate Physicsを有効、Object TypeをDynamicに。
   Enable Clusteringを有効、Damage Thresholdは最初は1000程度から試す。
6. Collectionの床への衝突を有効にする。静的な床を別に置く。
7. **ActorのTags** に `BridgeWall` を追加（Component Tagsではありません）。
8. BridgeReceiverは初期値 Radius=400cm、Strain=500000、Force=200000。

受信時、壁にExternalClusterStrainを与えてClusterを破壊し、半径内にRadial Forceを与えます。
壁が開始直後に崩れる場合はCluster/Threshold/床設定を確認。壊れない場合は
Geometry Collectionか、タグ、半径、Damage Thresholdを確認してください。
Chaosの挙動はアセット設定次第で、値の実機調整が必要です。

## 位置を合わせる

MCワールドへ入った時の足元がUE PlayerStartの足元に対応します。Yaw=0（Minecraftの南）が
UE +X。MCで南へ約3ブロック進んだ場所のTNTがUEの壁近くになります。
MC内にUEの壁は自動生成されません。MCとUEの地形同期は今回の範囲外です。

合わなくなった場合はMCワールド退出→UE Play停止→UE Play開始→MCワールド入場の順で
揃え直します。壁を復元する場合もUE Playを再開始してください。

# UE 0.18.1 compile hotfix

[UEBridge-update-0.18.1.zip](UEBridge-update-0.18.1.zip) / [導入手順](../docs/UPGRADE_0.18.1.md)。MODは0.18.0を継続使用してください。Windows UE実ビルドは未検証です。

# ダウンロード

**物理アイテム・60fps動画0.18.0：** [MOD](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/minecraft-ue-bridge-0.18.0.jar) / [UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.18.0.zip) / [一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UE-Minecraft-MVP-0.18.0.zip) / [導入と操作](../docs/UPGRADE_0.18.0.md)。0.17.0未導入から直接更新できます。砂・TNT・水／溶岩バケツ、表示切替、保存、種類／範囲別削除と取り消し、60fps無音AVIを追加。液体は有限格子、火・煙はメッシュ表現です。Windows UE実ビルド・GPU描画は未検証です。

**UE単独の描画・クラフト・基本回路更新0.17.0：** [MOD](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/minecraft-ue-bridge-0.17.0.jar) / [UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.17.0.zip) / [一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UE-Minecraft-MVP-0.17.0.zip) / [更新手順と対応範囲](../docs/UPGRADE_0.17.0.md)。MOD交換・新しいnative export・UE再ビルド・再取り込みが必要です。Minecraftはプレイ中に不要。全特殊機能・バニラ完全一致は未完成で、Windows UE実ビルド・描画は未検証です。

**地形読み込みのクラッシュ修正0.16.2：** [修正ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.16.2.zip) / [適用手順](../docs/UPGRADE_0.16.2.md)。液体などの裏面追加時のTArray自己参照Assertionを修正。0.16.1も含みます。MODは0.16.0のまま、UEだけを再ビルドしてください。修正後のWindows実起動は未検証です。

**0.16.0のUEビルド修正0.16.1：** [修正ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.16.1.zip) / [適用手順](../docs/UPGRADE_0.16.1.md)。ヘッダー・変数名・流体の共通関数・配列APIによるビルド停止を修正。MODは0.16.0のまま、今回の書き出しを使用してUEを再ビルドします。修正後のWindowsビルドは未検証です。

**ネイティブ操作・流体・時間更新0.16.0：** [MOD](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/minecraft-ue-bridge-0.16.0.jar) / [UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.16.0.zip) / [一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UE-Minecraft-MVP-0.16.0.zip) / [更新と確認手順](../docs/UPGRADE_0.16.0.md)。照準・しゃがみの端判定・モブの押し出しと死亡・向き、液体、時間サイクル、コマンドチャット、戦闘と落葉の粒子を追加・修正。MOD交換、新規native export、UE再ビルド・再取り込みが必要です。全種固有AI・特殊ブロック機能・原作と同一の流体挙動は未完成で、Windows UE 5.8.3の実ビルド・描画は未検証です。

**診断後の修正版0.15.4：** [MOD](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/minecraft-ue-bridge-0.15.4.jar) / [UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.15.4.zip) / [適用と確認項目](../docs/UPGRADE_0.15.4.md)。符号付き視点角・アイテムの両面設定・重なる面・アウトライン・ノックバック・基本AIを変更。MOD交換、新規native export、UE再ビルド・再取り込みが必要です。全種固有AIの移植・実機での全問題解消は未確認です。

**コード・実測取得用MOD 0.15.3：** [診断MOD](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/minecraft-ue-bridge-0.15.3.jar) / [取得手順](../docs/DIAGNOSTICS_0.15.3_JA.md)。`/uebridge diagnose export` でクラスデータ・モデル・約10秒のモブ状態をZIPにします。今回はMODのみで、UEの未解決問題の修正は診断後に行います。

**描画・入力・戦闘修正0.15.2：** [UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-update-0.15.2.zip) / [MOD 0.15.2](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/minecraft-ue-bridge-0.15.2.jar) / [更新手順](../docs/UPGRADE_0.15.2.md)。MOD交換・新しいnative export・UE再ビルド・再取り込みが必要です。Windowsの実描画・入力は未確認です。

**UE 5.8.3ビルド修正0.15.1：** 0.15.0を導入した後に[修正ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.15.0/downloads/UEBridge-build-fix-0.15.1.zip)のSourceを適用してください。MOD交換・再書き出しは不要です。[適用手順](../docs/BUILD_FIX_0.15.1.md)。Windows実ビルドは未確認です。

## 完了後の終了判定修正0.13.2

保存完了後のUE終了時アクセス違反を、現在の試行の完了記録とログを検証して区別します。

- [UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.13.2/downloads/UEBridge-update-0.13.2.zip)
- [保存済みマップの即時起動・更新手順](../docs/UPGRADE_0.13.2.md)

## モブ取り込み修正0.13.1

UE 5.8.3でのQuat／Rotator型違いによる停止を修正。0.13.0適用済みならPython 1ファイルの更新で適用できます。

- [UE更新ZIP 0.13.1](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.13.1/downloads/UEBridge-update-0.13.1.zip)
- [更新手順・検証](../docs/UPGRADE_0.13.1.md)

## UE照明マテリアル修正版0.12.4

VertexColorのRGBA出力指定による取り込み停止を修正します。0.12.3適用済みならZIP内のbridge_lighting_materials.pyとimport_minecraft_atlas.pyだけを上書きし再取り込みできます。C++再ビルド・MOD交換・再書き出しは不要です。修正後の実機取り込み完了・描画は未確認です。

- [UE更新ZIP 0.12.4](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.4/downloads/UEBridge-update-0.12.4.zip)
- [適用・再試行手順](../docs/UPGRADE_0.12.4.md)

## UE取り込みAPI修正版0.12.3

取り込みログで確認したGameplayStaticsのAttributeErrorを修正します。0.12.2適用済みならZIP内のimport_native_play.pyだけを上書きして再取り込みできます。MOD交換・再書き出しは不要です。修正後の実機取り込み・描画は未確認です。

- [UE更新ZIP 0.12.3](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.3/downloads/UEBridge-update-0.12.3.zip)
- [適用・再試行手順](../docs/UPGRADE_0.12.3.md)

## UE取り込み診断更新0.12.2

0.12.1のWindows UEビルド成功は利用者ログで確認しました。取り込みの根本原因は未特定です。専用ログとPython例外全文を保存する診断更新です。

- [UE更新ZIP 0.12.2](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.2/downloads/UEBridge-update-0.12.2.zip)
- [適用・再試行手順](../docs/UPGRADE_0.12.2.md)

## UE側ビルド修正版0.12.1

0.12.0のWindows UE 5.8.3ビルドで報告されたコンパイルエラーへ対処した更新です。MODは0.12.0のまま、既存UEプロジェクトへSourceと補助スクリプトを統合コピーします。旧配布物は保持しています。Windows UEビルド成功は利用者ログで確認しました。取り込み・描画は未確認です。

- [UE更新ZIP 0.12.1](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.1/downloads/UEBridge-update-0.12.1.zip)
- [SHA-256](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.1/downloads/UEBridge-update-0.12.1.zip.sha256)
- [具体的な上書き先・起動コマンド・検証範囲](../docs/UPGRADE_0.12.1.md)

## UE単独プレイ0.12.0

- [MOD 0.12.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.0/downloads/minecraft-ue-bridge-0.12.0.jar)
- [既存UEプロジェクト用更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.0/downloads/UEBridge-update-0.12.0.zip)
- [ソース＋MOD一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-native-play-0.12.0/downloads/UE-Minecraft-MVP-0.12.0.zip)
- [更新・書き出し・ダブルクリック起動の手順](../docs/UPGRADE_0.12.0.md)
- [対応範囲・残る機能・Windows実機の確認](../docs/NATIVE_PLAY.md)

Minecraftで `/uebridge native export` を1回実行し、完了後は **Play-Native.cmd** でUEを起動します。初回だけJSONを選び、次回からはダブルクリックで再開します。Minecraftを終了した状態で、UEへ直接入力し、手元の素材を使うHUD・インベントリ・音を表示／再生します。UE地形とプレイヤー・所持品・モブ・投下物を同じ保存へ記録します。

**MODとUEの両方を更新し、素材をnativeパッケージとして書き出してください。** 既存UE更新ZIPにはSource、ビルド・起動スクリプト、すべてのPython補助ファイルを含め、Content／Config／Saved／uproject／既存レベルは含めません。旧配布物は保持します。配布ブランチは `ue-native-play-0.12.0` です。

**Minecraftの完全移植には未達です。** オフハンド・クラフト・食料／経験値／防具・流体・全モブ固有AI等は未実装です。UE 5.8のWindowsビルド・実描画・IME・実FPSは未確認です。クラウドでの検証と実機確認の範囲は上記の手順に記載しています。

MODビルド、Java185件・Python123件、UEから独立したC++計算とPowerShellの構文・設定保持の検証が成功しました。実機向けのUE自動テストは追加済みですが、このクラウドでは実行していません。

## 更新版0.11.0

`UE映像待ち: EOFException` でMinecraftが黒画面になり、UE側は描画を続けている場合は、[映像再接続修正MOD 0.11.6](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/minecraft-ue-bridge-0.11.0-video-reconnect-fix.jar)へ差し替えます。GPU共有の切断を検出すると、次の接続をJPEGへ自動降格します。[差し替え手順](../docs/VIDEO_RECONNECT_FIX_0.11.6.md)。既存の0.11.0 JARと配布ZIPは保持します。

`control ue`でメモリ不足になった場合は[UEメモリ修正パッチ0.11.5](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/UEBridge-memory-fix-0.11.5.zip)を適用します。[2ファイルの上書き・再ビルド手順](../docs/MEMORY_FIX_0.11.5.md)。

素材取り込みには[Python修正パッチ0.11.4](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/UEBridge-lighting-import-fix-0.11.4.zip)を適用します。[3ファイルの上書き・再取り込み手順](../docs/LIGHTING_IMPORT_FIX_0.11.4.md)。

**UEビルドには[修正パッチ0.11.1](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/UEBridge-build-fix-0.11.1.zip)も適用してください。** MODは0.11.0のまま、UEの5ファイルを上書きして再ビルドします。[操作手順](../docs/BUILD_FIX_0.11.1.md)。旧ZIPは保持しています。

- [MOD 0.11.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/minecraft-ue-bridge-0.11.0.jar)
- [既存UE用更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/UEBridge-update-0.11.0.zip)
- [ソース＋MOD一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.11.0/downloads/UE-Minecraft-MVP-0.11.0.zip)
- [上書き対象・再書き出し・起動と性能確認の手順](../docs/UPGRADE_0.11.0.md)

ゾンビ・村人の召喚とモデル初期化、UE照明への復帰、Minecraftの光情報を使う照明OFF、投下・拾得を修正／実装しました。隠れた地形の面を省き、アトラスと地形ストリーミング、WindowsのD3D11／NVIDIA GPU共有映像を追加しています。GPU共有の条件が合わない場合はJPEGへ戻ります。

**MODとUEの両方の更新、UE再ビルド、ブロック・スキン・アイテム・モブの再書き出し／取り込みが必要です。** Sourceを統合コピーし、Content／Config／Saved／保存済みレベルを保持します。旧版ZIP・mainは保持します。

目標はRTX 5060、1080p、4～6チャンク、実映像30fps以上です。クラウドで可能なテストは行っていますが、**UE5.8ビルド・描画・Windows統合動作・実機FPSの目標達成は未確認**です。受信FPSと描画投入FPS、入力遅延の計測を追加しています。

MODビルド、Java152件・Python65件、独立C++の光・地形・召喚位置・映像タイミングの検証が成功しました。Windows x64のGPU用DLLはMODへ同梱しています。

## アイテム書き出し容量修正0.10.1

- [MOD 0.10.1](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.10.0/downloads/minecraft-ue-bridge-0.10.1.jar)
- [Python取り込み修正ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.10.0/downloads/UEBridge-item-export-fix-0.10.1.zip)
- [更新・再書き出し手順](../docs/ITEM_EXPORT_FIX_0.10.1.md)

`Item manifest exceeds 64 MiB` の修正。MODとimport_minecraft_items.pyを更新し、アイテムを再書き出しします。UEのC++ソースは0.10.0のまま、再ビルド不要です。旧配布物は保持します。

## 更新版0.10.0

- [MOD 0.10.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.10.0/downloads/minecraft-ue-bridge-0.10.0.jar)
- [既存UE用更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.10.0/downloads/UEBridge-update-0.10.0.zip)
- [ソース＋MOD一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.10.0/downloads/UE-Minecraft-MVP-0.10.0.zip)
- [上書き対象・再書き出し・取り込み・操作順](../docs/UPGRADE_0.10.0.md)

色・アウトライン・腕の残像対策、主手のネイティブ持ち物モデル、地上モブのスポーンエッグ、開閉／スイッチ音、クリエイティブ限定の飛行。0.9.4までの修正を含み、旧パッチは不要です。
**MODとUE両方の更新・UE再ビルド・素材の再書き出し／取り込みが必要です。**
Sourceを削除せず統合コピーし、既存Content/Config/Saved/レベルを保持します。
Java108件・Python49件と独立C++計算が成功。UE5.8のビルド・描画・Windows統合動作は未確認です。
全アイテムの全状態や全モブ固有AIの完成ではありません。対応範囲は操作手順に記載しています。
新しい配布ブランチは `ue-bridge-0.10.0`。以前のZIPとmainは保持します。

## UE腕・持ち物マテリアル修正0.9.4

- [UE修正パッチZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/UEBridge-material-fix-0.9.4.zip)
- [1ファイルの上書き・再ビルドとIDLE時の操作](../docs/UE_MATERIAL_FIX_0.9.4.md)

BridgeCharacter.cppの動的マテリアル親の警告を修正します。
MODは0.9.3、UEの他のファイルと素材は保持します。UEビルド・描画はクラウドでは未確認です。

## MOD・取り込みスクリプト修正0.9.3

- [MOD 0.9.3](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/minecraft-ue-bridge-0.9.3.jar)
- [取り込みスクリプト更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/UEBridge-texture-import-fix-0.9.3.zip)
- [具体的な操作順](../docs/UPGRADE_0.9.3.md)

ブロック状態のJava列挙名とMinecraft保存用の名前の違いを修正。
MODとimport_minecraft_textures.pyを更新し、ブロック素材を再書き出し・取り込みします。
今回の修正だけならUE再ビルドは不要です。UEソースは0.9.0＋修正0.9.2を使用します。
MODビルド・Java97件・Python42件成功。UE取り込み・描画の実機確認は未実施です。

## 更新版0.9.0

**UEビルドには[累積修正パッチ0.9.2](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/UEBridge-build-fix-0.9.2.zip)も適用してください。**
UE5.8でのヘッダー順序・JSONキー変換・雲フラグ・Role変数のエラーを修正。
0.9.1の存在しないGetDataメンバー呼び出しも修正し、0.9.1の変更をすべて含みます。
MODは0.9.0のまま、UEの3ファイルを上書きして再ビルドします。[適用手順](../docs/BUILD_FIX_0.9.2.md)。

- [MOD 0.9.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/minecraft-ue-bridge-0.9.0.jar)
- [既存UE用更新ZIP 0.9.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/UEBridge-update-0.9.0.zip)
- [ソース＋MOD一式ZIP 0.9.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.9.0/downloads/UE-Minecraft-MVP-0.9.0.zip)

一人称のFOV70投影、破片調整、通常ブロックのモデル・当たり判定・設置、
地上モブの基本AI、照明オフと実際のMinecraftの空の合成。
[上書き対象・操作順・診断](../docs/UPGRADE_0.9.0.md) / [ブロック対象範囲](../docs/BLOCK_SUPPORT_0.9.0.md)。
モブ固有AI・飛行／水中・装備等の描画、全アイテム、全ブロック固有挙動は未完成です。

MODとUEを両方更新し、UEを閉じて再ビルドします。Sourceは削除せず統合コピーします。
ブロック・スキン・モブ素材をMC専用環境から再書き出し、UEに再取り込みしてください。
更新ZIPはContent/Config/Saved/uprojectを含まず、既存レベル・素材を保持します。
0.8.2の粒子設定修正を含むため、その旧パッチを重ねて適用する必要はありません。
配布ブランチは `ue-bridge-0.9.0`。旧版ZIP・mainは保持します。

クラウドでMODビルド、Java95件/Python41件、独立C++計算53項目とマスク15往復が成功。
実際の1.21.11ローカル素材1008静的モデルIDの解決・検証も成功しました。
**UE5.8のビルド・描画・Windows統合動作は未確認です。**

## 更新版 0.8.0

**粒子素材の設定には[修正パッチ0.8.2](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.8.0/downloads/UEBridge-particle-setup-fix-0.8.2.zip)も適用してください。**
UE 5.8のUV入力接続エラーと0.8.1の`default_value`プロパティエラーを修正します。[適用手順](../docs/PARTICLE_SETUP_FIX_0.8.2.md)。
setup_vanilla_effects.pyだけを更新します。MOD/C++は0.8.0のまま、再ビルドは不要です。

[MOD 0.8.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.8.0/downloads/minecraft-ue-bridge-0.8.0.jar)

[UE更新ZIP 0.8.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.8.0/downloads/UEBridge-update-0.8.0.zip)

[ソース＋MOD一式ZIP 0.8.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/ue-bridge-0.8.0/downloads/UE-Minecraft-MVP-0.8.0.zip)

粒子診断・素材設定、しゃがみジャンプ、着地音、腕・持ち物・胴体、目線とダッシュFOVを改善。
設定済み前進キーの二度押しダッシュを追加。[導入と実機チェック](../docs/UPGRADE_0.8.0.md)。
配布ブランチは `ue-bridge-0.8.0`。従来のmainと旧版配布物は保持しています。
MODとUEを両方更新し、UEを閉じて再ビルドした後、粒子素材を再設定してください。
UE更新ZIPはContent/Config/Saved/uprojectを含みません。Sourceは削除せず、使用中プロジェクトへ統合コピーします。
0.7.1の修正を含むため、旧パッチの追加適用は不要です。UE5.8のビルド・描画はクラウドでは未検証です。
MODビルド、Java78件/Python27件、独立C++計算チェック35項目成功。粒子の実機不表示の原因は診断値で確認します。

## 過去版 0.7.0

**UEビルドには[修正パッチ0.7.1](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-build-fix-0.7.1.zip)も適用してください。**
粒子コードのC2668を修正し、GPU終了処理の旧APIを更新。MODは0.7.0のまま。[適用手順](../docs/BUILD_FIX_0.7.1.md)。

[MOD 0.7.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.7.0.jar)

[UE更新ZIP 0.7.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-update-0.7.0.zip)

[ソース＋MOD一式ZIP 0.7.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UE-Minecraft-MVP-0.7.0.zip)

自分のスキン付き腕・全身、Minecraftの設定済みキーでの視点切り替え、バニラの音とブロック粒子。
[更新・書き出し・取り込み・テスト手順](../docs/UPGRADE_0.7.0.md)。MODとUE両方の更新・UE再ビルドが必要。
保存済みレベルを使用し、Sourceを削除せずZIP内のファイルを統合コピーしてください。
ビルド補助は必要なProceduralMeshComponentプラグインのみuprojectへ追加し、元ファイルをバックアップします。
今回はスキンと粒子のために素材を書き出し直します。配布物にユーザーのスキン・Minecraft音声/画像は含みません。
Java63件/Python21件成功。UE5.8のビルド・統合テストは実機で必要です。

## 過去版 0.6.0

**UEビルドには[修正パッチ0.6.1](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-build-fix-0.6.1.zip)も適用してください。** C4458のMesh/Owner名前衝突を修正。MOD0.6.0はそのまま。[適用手順](../docs/BUILD_FIX_0.6.1.md)。

[MOD 0.6.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.6.0.jar)

[UE更新ZIP 0.6.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-update-0.6.0.zip)

[ソース＋MOD一式ZIP 0.6.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UE-Minecraft-MVP-0.6.0.zip)

照準枠、UEでの設置・破壊、腕と手・持ち物、移動調整、非同期映像転送と遅延計測。
**MODとUEの両方を更新し、UEを再ビルド**してください。[更新・操作・テスト手順](../docs/UPGRADE_0.6.0.md)。
Sourceは使用中uprojectの横へ統合コピー。既存Content/Config/Savedを保持し、素材再インポートは不要。
Java47件/Python10件成功。UE5.8ビルド・GPU・描画・統合検証はPCで必要です。

[UE移動修正ZIP 0.5.2](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-movement-fix-0.5.2.zip)：初回着地後の移動停止とジャンプ速度を修正。ファイルを上書きしUE再ビルド。MOD0.5.0は維持。

## UEビルド修正 0.5.1

[UEビルド修正ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-build-fix-0.5.1.zip)

0.5.0でBridgeWorld.cppのC4458/C2064が出る場合に適用。Sourceを上書きして再ビルド。
MOD0.5.0と保存済みレベル/素材は変更不要です。下の0.5.0アーカイブには修正が入っていません。

## 過去版 0.5.0

[MOD 0.5.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.5.0.jar)

[UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-update-0.5.0.zip)

[ソース＋MOD一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UE-Minecraft-MVP-0.5.0.zip)

Minecraft入力/HUD、UE移動・衝突。初期地形転送後のUE地形をPlay中に保持。
[更新・起動・テスト手順](../docs/UPGRADE_0.5.0.md)。MODとUE両方の更新・UE再ビルドが必要。
Sourceフォルダも必ずコピーしてください。Content/Config/Savedや素材を置換しません。
素材取り込み修正0.4.3を含みます。既に素材が動いている場合は再インポート不要。
Java39件/Python10件成功、UE実機ビルド・移動/衝突確認は必要です。


## 過去版 0.4.0

[Minecraft MOD 0.4.0](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.4.0.jar)

[UE更新ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-update-0.4.0.zip)

[ソース＋MOD一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UE-Minecraft-MVP-0.4.0.zip)

ブロックテクスチャのローカル書き出し/UE取り込み、映像品質3段階、露出調整。バニラ素材は配布物に含めません。
**MODとUEを両方更新し、UEを閉じた状態で再ビルド**してください。
[導入・素材取り込み・テスト手順](../docs/UPGRADE_0.4.0.md)。既存のレベルを保持する更新パッチです。
0.3.1のPythonパス修正を含みます。Java/Pythonテスト成功。今回のUE実機ビルド・描画確認は必要です。


## マテリアル設定スクリプト修正版 0.3.1

[設定スクリプト修正ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-material-setup-fix-0.3.1.zip)

UEの`Paths.project_file_path`のAttributeErrorを修正。ZIP内の`setup_world_bridge.py`だけを使用中のUEBridgeへ上書きし、
Pythonモードで再実行してください。MOD 0.3.0とUE C++はそのままで、再ビルドは不要です。
下の0.3.0 ZIPは以前のスクリプトを含むため、この修正も適用してください。実UE実行の確認は必要です。

## 過去版 0.3.0

[Minecraft MOD 0.3.0のJAR](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/minecraft-ue-bridge-0.3.0.jar)

[UE側更新パッチ＋ビルド補助](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-update-0.3.0.zip)

[ソースとMODの一式ZIP](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UE-Minecraft-MVP-0.3.0.zip)

**今回はMODとUEの両方を更新します。** 周辺ワールド自動同期・UE映像のMinecraft HUD表示・しゃがみを追加。
[導入・テスト手順](../docs/UPGRADE_0.3.0.md)。UEパッチはSourceと補助スクリプトのみで、Content/Config/Savedを含みません。
MODビルドと通信テストをクラウドで確認。UEの実機ビルド・描画検証は必要です。
以下の0.2.xは過去版です。

## UEカメラ左右修正 0.2.2

[UE側ソース更新パッチ](https://github.com/TSUNAMI-SMP/UE-MINECRAFT/raw/refs/heads/main/downloads/UEBridge-camera-fix-0.2.2.zip) / [更新・低FPS切り分け手順](../docs/CAMERA_FIX_0.2.2.md)

MOD 0.2.1はそのまま使用します。UE Editorを閉じ、パッチのSourceを使用中のUEBridgeへコピーして再ビルドしてください。Content/Config/Savedは含まず、作成したレベル/アセットを置き換えません。UE実機ビルド・動作は未検証です。数FPSについては、先にエディタの背景CPU抑制をOFFにして確認してください。

## 起動修正版 0.2.1（過去版）

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

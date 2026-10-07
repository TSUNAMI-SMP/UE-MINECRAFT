// Compile/run using UE Session Frontend Automation; not executable in the cloud without UE.
#if WITH_DEV_AUTOMATION_TESTS
#include "BridgeProtocol.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace {
TSharedPtr<FJsonObject> PacketJson(const FString& Text) {
    TSharedPtr<FJsonObject> Result; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result); return Result;
}
const TCHAR* Input = TEXT("{\"v\":1,\"kind\":\"input\",\"session\":\"00000000-0000-4000-8000-000000000001\",\"seq\":1,\"x\":1,\"y\":2,\"z\":3,\"yaw\":90,\"pitch\":-30,\"forward\":1,\"right\":0,\"jump\":true}");
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeInputTest, "UEBridge.Protocol.InputAndAxes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeInputTest::RunTest(const FString& Parameters) {
    FBridgePacket P; TestTrue(TEXT("Valid input"), BridgeProtocol::Parse(PacketJson(Input), P));
    TestTrue(TEXT("Axes and 100cm scale"), BridgeProtocol::ToUnreal(P.Position, FVector(10,20,30)).Equals(FVector(310,-80,230)));
    TestTrue(TEXT("Jump"), P.Jump); TestEqual(TEXT("Yaw"), P.Yaw, 90.0);
    auto Object = PacketJson(Input); Object->SetStringField(TEXT("yaw"), TEXT("90"));
    TestFalse(TEXT("String numbers rejected"), BridgeProtocol::Parse(Object, P));
    Object = PacketJson(Input); Object->SetNumberField(TEXT("seq"), 1.5);
    TestFalse(TEXT("Fractional sequence rejected"), BridgeProtocol::Parse(Object, P));
    Object = PacketJson(Input); Object->SetNumberField(TEXT("pitch"), 91);
    TestFalse(TEXT("Pitch outside range rejected"), BridgeProtocol::Parse(Object, P));
    Object = PacketJson(Input); Object->SetStringField(TEXT("jump"), TEXT("true"));
    TestFalse(TEXT("String boolean rejected"), BridgeProtocol::Parse(Object, P));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeHandednessTest, "UEBridge.Protocol.CameraHandedness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeHandednessTest::RunTest(const FString& Parameters) {
    TestTrue(TEXT("MC south maps to UE forward"), BridgeProtocol::ToDirection(FVector(0,0,1)).Equals(FVector(1,0,0)));
    TestTrue(TEXT("MC right at yaw zero maps to UE right"), BridgeProtocol::ToDirection(FVector(-1,0,0)).Equals(FVector(0,1,0)));
    for (double Yaw : {0.0, 45.0, 90.0, -90.0, 180.0}) for (double Pitch : {0.0, -30.0, 30.0}) {
        const double Y = FMath::DegreesToRadians(Yaw), P = FMath::DegreesToRadians(Pitch);
        const FVector MinecraftForward(-FMath::Sin(Y)*FMath::Cos(P), -FMath::Sin(P), FMath::Cos(Y)*FMath::Cos(P));
        TestTrue(TEXT("Camera forward matches converted world/projectile direction"),
            BridgeProtocol::ToRotation(Yaw, Pitch).Vector().Equals(BridgeProtocol::ToDirection(MinecraftForward), 1e-6));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeEventTest, "UEBridge.Protocol.Events", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeEventTest::RunTest(const FString& Parameters) {
    auto P = PacketJson(Input); P->SetStringField(TEXT("kind"), TEXT("event")); P->SetStringField(TEXT("event"), TEXT("tnt_ignite"));
    FBridgePacket Out; TestFalse(TEXT("Missing event UUID"), BridgeProtocol::Parse(P, Out));
    P->SetStringField(TEXT("eventId"), TEXT("00000000-0000-4000-8000-000000000002"));
    TestTrue(TEXT("TNT event"), BridgeProtocol::Parse(P, Out));
    P->SetStringField(TEXT("event"), TEXT("bow_fire")); P->SetNumberField(TEXT("dx"), 0); P->SetNumberField(TEXT("dy"), 0);
    P->SetNumberField(TEXT("dz"), 1); P->SetNumberField(TEXT("pull"), 1);
    TestTrue(TEXT("Normalized bow vector"), BridgeProtocol::Parse(P, Out));
    P->SetNumberField(TEXT("dz"), 0); TestFalse(TEXT("Zero bow direction"), BridgeProtocol::Parse(P, Out));
    P->SetStringField(TEXT("event"), TEXT("unknown")); TestFalse(TEXT("Unknown event"), BridgeProtocol::Parse(P, Out));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeSnapshotTest, "UEBridge.Protocol.Snapshot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeSnapshotTest::RunTest(const FString& Parameters) {
    const FString Text = TEXT("{\"v\":1,\"kind\":\"event\",\"session\":\"00000000-0000-4000-8000-000000000001\",\"seq\":5,\"eventId\":\"00000000-0000-4000-8000-000000000002\",\"event\":\"block_snapshot\",\"x\":0,\"y\":0,\"z\":0,\"snapshotId\":\"00000000-0000-4000-8000-000000000003\",\"snapshotSeq\":3,\"batchIndex\":0,\"totalBatches\":1,\"blocks\":[[1,2,3,16711680]]}");
    auto P = PacketJson(Text); FBridgePacket Out;
    TestTrue(TEXT("Snapshot"), BridgeProtocol::Parse(P, Out)); TestEqual(TEXT("Block count"), Out.Blocks.Num(), 1);
    P->SetNumberField(TEXT("batchIndex"), 1); TestFalse(TEXT("Index outside total"), BridgeProtocol::Parse(P, Out));
    P = PacketJson(Text); P->SetNumberField(TEXT("snapshotSeq"), 6); TestFalse(TEXT("Generation after packet"), BridgeProtocol::Parse(P, Out));
    P = PacketJson(Text); P->SetNumberField(TEXT("totalBatches"), 1000000); TestFalse(TEXT("Batch limit"), BridgeProtocol::Parse(P, Out));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgePoseTest, "UEBridge.Protocol.MinecraftPose", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgePoseTest::RunTest(const FString& Parameters) {
    auto P=PacketJson(Input); FBridgePacket Out;
    TestTrue(TEXT("Older MOD input uses standing defaults"),BridgeProtocol::Parse(P,Out));
    TestEqual(TEXT("Standing eye"),Out.EyeHeight,1.62);
    P->SetBoolField(TEXT("sneak"),true); P->SetNumberField(TEXT("eyeHeight"),1.27); P->SetNumberField(TEXT("bodyHeight"),1.5);
    TestTrue(TEXT("Sneaking pose"),BridgeProtocol::Parse(P,Out)); TestTrue(TEXT("Sneak"),Out.Sneak);
    TestEqual(TEXT("Crouch eye"),Out.EyeHeight,1.27); TestEqual(TEXT("Crouch body"),Out.BodyHeight,1.5);
    P->SetStringField(TEXT("sneak"),TEXT("true")); TestFalse(TEXT("String sneak rejected"),BridgeProtocol::Parse(P,Out));
    P->SetBoolField(TEXT("sneak"),true); P->SetNumberField(TEXT("bodyHeight"),-1); TestFalse(TEXT("Negative body rejected"),BridgeProtocol::Parse(P,Out));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeWorldProtocolTest, "UEBridge.Protocol.WorldCell", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeWorldProtocolTest::RunTest(const FString& Parameters) {
    const FString Text=TEXT("{\"v\":1,\"kind\":\"event\",\"session\":\"00000000-0000-4000-8000-000000000001\",\"seq\":5,\"eventId\":\"00000000-0000-4000-8000-000000000002\",\"event\":\"world_cell\",\"cellX\":-1,\"cellY\":0,\"cellZ\":1,\"x\":0,\"y\":0,\"z\":0,\"snapshotId\":\"00000000-0000-4000-8000-000000000003\",\"snapshotSeq\":3,\"batchIndex\":0,\"totalBatches\":1,\"blocks\":[[1,2,3,16711680,1,0.5,1]]}");
    auto P=PacketJson(Text); FBridgePacket Out;
    TestTrue(TEXT("World cell with half block"),BridgeProtocol::Parse(P,Out));
    TestTrue(TEXT("Shape dimensions"),Out.Blocks.Num()==1 && Out.Blocks[0].Size.Equals(FVector(1,.5,1)));
    TestTrue(TEXT("Negative cell coordinate"),Out.Cell==FIntVector(-1,0,1));
    P->SetNumberField(TEXT("cellX"),.5); TestFalse(TEXT("Fractional cell rejected"),BridgeProtocol::Parse(P,Out));
    P=PacketJson(Text); P->SetNumberField(TEXT("totalBatches"),1025); TestFalse(TEXT("World batch limit"),BridgeProtocol::Parse(P,Out));
    P=PacketJson(Text); P->SetStringField(TEXT("event"),TEXT("world_scope")); P->SetNumberField(TEXT("radius"),2); P->SetNumberField(TEXT("halfHeight"),1);
    TestTrue(TEXT("World scope"),BridgeProtocol::Parse(P,Out));
    P->SetNumberField(TEXT("radius"),13); TestFalse(TEXT("World scope limit"),BridgeProtocol::Parse(P,Out));
    P->SetStringField(TEXT("event"),TEXT("world_clear")); TestTrue(TEXT("World clear"),BridgeProtocol::Parse(P,Out));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeVideoConfigTest, "UEBridge.Protocol.VideoConfig", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeVideoConfigTest::RunTest(const FString& Parameters) {
    auto P=PacketJson(Input); FBridgePacket Out;
    P->SetStringField(TEXT("kind"),TEXT("event")); P->SetStringField(TEXT("event"),TEXT("video_config"));
    P->SetStringField(TEXT("eventId"),TEXT("00000000-0000-4000-8000-000000000002"));
    P->SetNumberField(TEXT("width"),960); P->SetNumberField(TEXT("height"),540);
    P->SetNumberField(TEXT("fps"),20); P->SetNumberField(TEXT("quality"),85); P->SetNumberField(TEXT("exposure"),1);
    TestTrue(TEXT("Quality configuration"),BridgeProtocol::Parse(P,Out));
    TestEqual(TEXT("Width"),Out.VideoWidth,960); TestEqual(TEXT("Exposure"),Out.VideoExposure,1.0);
    P->SetBoolField(TEXT("lighting"),false);P->SetBoolField(TEXT("vanillaSky"),true);
    TestTrue(TEXT("Unlit native sky"),BridgeProtocol::Parse(P,Out));TestTrue(TEXT("Sky flag"),Out.VanillaSky);
    P->SetBoolField(TEXT("lighting"),true);TestFalse(TEXT("Lit sky mode is contradictory"),BridgeProtocol::Parse(P,Out));
    P->SetBoolField(TEXT("lighting"),false);P->SetStringField(TEXT("vanillaSky"),TEXT("true"));
    TestFalse(TEXT("Typed sky flag"),BridgeProtocol::Parse(P,Out));P->SetBoolField(TEXT("vanillaSky"),true);
    P->SetNumberField(TEXT("particleScale"),0);TestFalse(TEXT("Particle scale bound"),BridgeProtocol::Parse(P,Out));
    P->SetNumberField(TEXT("particleScale"),.75);
    P->SetNumberField(TEXT("fps"),61); TestFalse(TEXT("FPS bound"),BridgeProtocol::Parse(P,Out));
    P->SetNumberField(TEXT("fps"),20); P->SetStringField(TEXT("exposure"),TEXT("1"));
    TestFalse(TEXT("Typed exposure"),BridgeProtocol::Parse(P,Out));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeModelRowsTest, "UEBridge.Protocol.ModelRows", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeModelRowsTest::RunTest(const FString& Parameters) {
    const FString Text=TEXT("{\"v\":1,\"kind\":\"event\",\"session\":\"00000000-0000-4000-8000-000000000001\",\"seq\":5,\"eventId\":\"00000000-0000-4000-8000-000000000002\",\"event\":\"world_cell_physics\",\"cellX\":0,\"cellY\":0,\"cellZ\":0,\"x\":0,\"y\":0,\"z\":0,\"snapshotId\":\"00000000-0000-4000-8000-000000000003\",\"snapshotSeq\":3,\"batchIndex\":0,\"totalBatches\":8192,\"blocks\":[[1.5,2.5,3.5,16777215,1,1,1,\"minecraft:oak_stairs\",false,1,2,3,\"facing=east,half=bottom,shape=straight,waterlogged=false\",1]]}");
    FBridgePacket Out;TestTrue(TEXT("Baked model row"),BridgeProtocol::Parse(PacketJson(Text),Out));
    TestTrue(TEXT("State and role preserved"),Out.Blocks.Num()==1 && Out.Blocks[0].Role==1 && Out.Blocks[0].StateKey.Contains(TEXT("facing=east")));
    TestFalse(TEXT("Cross-cell owner rejected"),BridgeProtocol::Parse(PacketJson(Text.Replace(TEXT("false,1,2,3"),TEXT("false,8,2,3"))),Out));
    TestFalse(TEXT("State cannot contain path"),BridgeProtocol::Parse(PacketJson(Text.Replace(TEXT("facing=east"),TEXT("facing=../east"))),Out));
    TestFalse(TEXT("Render row cannot collide"),BridgeProtocol::Parse(PacketJson(Text.Replace(TEXT("false,1,2,3"),TEXT("true,1,2,3"))),Out));
    TestFalse(TEXT("Unknown role"),BridgeProtocol::Parse(PacketJson(Text.Replace(TEXT("false\",1]]"),TEXT("false\",4]]"))),Out));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeMobPacketsTest, "UEBridge.Protocol.MobPackets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeMobPacketsTest::RunTest(const FString& Parameters) {
    auto P=PacketJson(Input);FBridgePacket Out;
    P->SetStringField(TEXT("kind"),TEXT("event"));P->SetStringField(TEXT("event"),TEXT("mob_spawn"));
    P->SetStringField(TEXT("eventId"),TEXT("00000000-0000-4000-8000-000000000002"));
    P->SetStringField(TEXT("importId"),TEXT("00000000-0000-4000-8000-000000000003"));
    P->SetStringField(TEXT("mobId"),TEXT("ABCDEF01-2345-4678-9ABC-DEF012345678"));
    P->SetStringField(TEXT("mobType"),TEXT("minecraft:zombie"));
    P->SetStringField(TEXT("appearance"),FString::ChrN(64,TCHAR('a')));
    P->SetNumberField(TEXT("width"),.6);P->SetNumberField(TEXT("height"),1.95);
    P->SetNumberField(TEXT("health"),20);P->SetNumberField(TEXT("maxHealth"),20);
    P->SetNumberField(TEXT("speed"),.23);P->SetNumberField(TEXT("damage"),3);
    P->SetBoolField(TEXT("hostile"),true);P->SetBoolField(TEXT("baby"),false);
    TestTrue(TEXT("Native mob snapshot and uppercase UUID"),BridgeProtocol::Parse(P,Out));
    TestTrue(TEXT("Mob type and dimensions retained"),Out.Kind==EBridgeKind::MobSpawn && Out.Mob.Type==TEXT("minecraft:zombie") && FMath::IsNearlyEqual(Out.Mob.Height,1.95f));
    P->SetNumberField(TEXT("health"),21);TestFalse(TEXT("Health exceeds maximum"),BridgeProtocol::Parse(P,Out));P->SetNumberField(TEXT("health"),20);
    P->SetStringField(TEXT("hostile"),TEXT("true"));TestFalse(TEXT("Typed hostility"),BridgeProtocol::Parse(P,Out));P->SetBoolField(TEXT("hostile"),true);
    P->SetStringField(TEXT("appearance"),TEXT("../local-resource"));TestFalse(TEXT("Appearance must be hash"),BridgeProtocol::Parse(P,Out));
    P->SetStringField(TEXT("event"),TEXT("player_respawn"));TestTrue(TEXT("Respawn requires valid import"),BridgeProtocol::Parse(P,Out));
    P->RemoveField(TEXT("importId"));TestFalse(TEXT("Respawn import required"),BridgeProtocol::Parse(P,Out));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeTextureProtocolTest, "UEBridge.Protocol.TexturedCell", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeTextureProtocolTest::RunTest(const FString& Parameters) {
    const FString Text=TEXT("{\"v\":1,\"kind\":\"event\",\"session\":\"00000000-0000-4000-8000-000000000001\",\"seq\":5,\"eventId\":\"00000000-0000-4000-8000-000000000002\",\"event\":\"world_cell_textured\",\"cellX\":0,\"cellY\":0,\"cellZ\":0,\"x\":0,\"y\":0,\"z\":0,\"snapshotId\":\"00000000-0000-4000-8000-000000000003\",\"snapshotSeq\":3,\"batchIndex\":0,\"totalBatches\":2048,\"blocks\":[[1,2,3,16711680,1,1,1,\"minecraft:stone\"]]}");
    auto P=PacketJson(Text); FBridgePacket Out;
    TestTrue(TEXT("Textured row"),BridgeProtocol::Parse(P,Out));
    TestTrue(TEXT("Block identity"),Out.Blocks.Num()==1 && Out.Blocks[0].BlockId==TEXT("minecraft:stone"));
    P->SetStringField(TEXT("event"),TEXT("world_cell"));
    TestFalse(TEXT("Legacy row cannot carry an ID"),BridgeProtocol::Parse(P,Out));
    P=PacketJson(Text.Replace(TEXT("minecraft:stone"),TEXT("minecraft:bad:id")));
    TestFalse(TEXT("Invalid block identifier"),BridgeProtocol::Parse(P,Out));
    P=PacketJson(Text); P->SetNumberField(TEXT("totalBatches"),2049);
    TestFalse(TEXT("Textured batch bound"),BridgeProtocol::Parse(P,Out));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeAuthorityProtocolTest, "UEBridge.Protocol.Authority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeAuthorityProtocolTest::RunTest(const FString& Parameters) {
    auto P=PacketJson(Input); FBridgePacket Out;
    P->SetBoolField(TEXT("controller"),true);
    TestTrue(TEXT("Controller input"),BridgeProtocol::Parse(P,Out)); TestTrue(TEXT("Controller flag"),Out.Controller);
    P->SetStringField(TEXT("controller"),TEXT("true")); TestFalse(TEXT("Typed controller"),BridgeProtocol::Parse(P,Out));
    P=PacketJson(Input); P->SetStringField(TEXT("kind"),TEXT("event"));
    P->SetStringField(TEXT("eventId"),TEXT("00000000-0000-4000-8000-000000000002"));
    P->SetStringField(TEXT("event"),TEXT("world_begin"));
    TestFalse(TEXT("Import ID required"),BridgeProtocol::Parse(P,Out));
    P->SetStringField(TEXT("importId"),TEXT("00000000-0000-4000-8000-000000000003"));
    P->SetNumberField(TEXT("ox"),1);P->SetNumberField(TEXT("oy"),2);P->SetNumberField(TEXT("oz"),3);
    TestTrue(TEXT("Import begin"),BridgeProtocol::Parse(P,Out));
    P->SetStringField(TEXT("event"),TEXT("world_commit"));P->SetNumberField(TEXT("cells"),75);
    TestTrue(TEXT("Import commit"),BridgeProtocol::Parse(P,Out));
    P->SetNumberField(TEXT("cells"),75.5);TestFalse(TEXT("Fractional count"),BridgeProtocol::Parse(P,Out));
    const FString Cell=TEXT("{\"v\":1,\"kind\":\"event\",\"session\":\"00000000-0000-4000-8000-000000000001\",\"seq\":5,\"eventId\":\"00000000-0000-4000-8000-000000000002\",\"event\":\"world_cell_physics\",\"cellX\":0,\"cellY\":0,\"cellZ\":0,\"x\":0,\"y\":0,\"z\":0,\"snapshotId\":\"00000000-0000-4000-8000-000000000003\",\"snapshotSeq\":3,\"batchIndex\":0,\"totalBatches\":1,\"blocks\":[[1,2,3,16711680,1,1,1,\"minecraft:stone\",true]]}");
    TestTrue(TEXT("Physics row"),BridgeProtocol::Parse(PacketJson(Cell),Out));
    TestTrue(TEXT("Collision flag"),Out.Blocks.Num()==1 && Out.Blocks[0].Collision);
    auto Owned=PacketJson(Cell);auto Row=Owned->GetArrayField(TEXT("blocks"))[0]->AsArray();
    Row.Add(MakeShared<FJsonValueNumber>(1));Row.Add(MakeShared<FJsonValueNumber>(2));Row.Add(MakeShared<FJsonValueNumber>(3));
    TArray<TSharedPtr<FJsonValue>> Rows;Rows.Add(MakeShared<FJsonValueArray>(Row));Owned->SetArrayField(TEXT("blocks"),Rows);
    TestTrue(TEXT("Exact shape owner"),BridgeProtocol::Parse(Owned,Out));
    TestTrue(TEXT("Partial shape keeps its source voxel"),Out.Blocks.Num()==1 && Out.Blocks[0].HasSourceBlock && Out.Blocks[0].SourceBlock==FIntVector(1,2,3));
    Row[9]=MakeShared<FJsonValueNumber>(8);Rows[0]=MakeShared<FJsonValueArray>(Row);Owned->SetArrayField(TEXT("blocks"),Rows);
    TestFalse(TEXT("Owner outside its source cell"),BridgeProtocol::Parse(Owned,Out));
    TestFalse(TEXT("Collision type"),BridgeProtocol::Parse(PacketJson(Cell.Replace(TEXT("true"),TEXT("1"))),Out));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeBlockActionProtocolTest,"UEBridge.Protocol.BlockActions",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeBlockActionProtocolTest::RunTest(const FString& Parameters) {
    auto P=PacketJson(Input);FBridgePacket Out;
    P->SetStringField(TEXT("kind"),TEXT("event"));P->SetStringField(TEXT("event"),TEXT("block_action"));
    P->SetStringField(TEXT("eventId"),TEXT("00000000-0000-4000-8000-000000000002"));
    P->SetStringField(TEXT("importId"),TEXT("00000000-0000-4000-8000-000000000003"));
    P->SetStringField(TEXT("action"),TEXT("place"));P->SetStringField(TEXT("heldItem"),TEXT("minecraft:stone"));
    P->SetStringField(TEXT("heldBlock"),TEXT("minecraft:stone"));P->SetNumberField(TEXT("heldColor"),0xaaaaaa);
    TestTrue(TEXT("Valid placement request"),BridgeProtocol::Parse(P,Out));
    TestEqual(TEXT("Selected block"),Out.HeldBlock,FString(TEXT("minecraft:stone")));
    P->SetStringField(TEXT("action"),TEXT("explode"));TestFalse(TEXT("Unknown action"),BridgeProtocol::Parse(P,Out));
    P->SetStringField(TEXT("action"),TEXT("break"));P->SetStringField(TEXT("heldBlock"),TEXT("../bad"));
    TestFalse(TEXT("Bad block ID"),BridgeProtocol::Parse(P,Out));
    P->SetStringField(TEXT("heldBlock"),TEXT(""));TestTrue(TEXT("Empty hand breaks"),BridgeProtocol::Parse(P,Out));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeCompactCellTest,"UEBridge.Protocol.CompactNativeCell",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeCompactCellTest::RunTest(const FString& Parameters) {
    const FString Text=TEXT("{\"v\":1,\"kind\":\"event\",\"session\":\"00000000-0000-4000-8000-000000000001\",\"seq\":5,\"eventId\":\"00000000-0000-4000-8000-000000000002\",\"event\":\"world_cell_compact\",\"cellX\":-1,\"cellY\":0,\"cellZ\":1,\"x\":0,\"y\":0,\"z\":0,\"ox\":0,\"oy\":0,\"oz\":0,\"snapshotId\":\"00000000-0000-4000-8000-000000000003\",\"snapshotSeq\":3,\"batchIndex\":0,\"totalBatches\":1,\"palette\":[[\"minecraft:oak_stairs\",\"facing=east,half=bottom,shape=straight,waterlogged=false\",16777215,0,0]],\"blocks\":[[511,0,12,7]]}");
    FBridgePacket Out;TestTrue(TEXT("Dictionary native cell"),BridgeProtocol::Parse(PacketJson(Text),Out));
    TestTrue(TEXT("Compact voxel restores exact absolute owner"),Out.Blocks.Num()==1 && Out.Blocks[0].SourceBlock==FIntVector(-1,7,15));
    if(Out.Blocks.Num()==1) {
        const FBridgeBlock& Block=Out.Blocks[0];
        TestTrue(TEXT("Native roles and light metadata"),Block.Role==1 && Block.NativeCompact && Block.HasLight && Block.HasSourceBlock);
        TestEqual(TEXT("Sky light"),int32(Block.SkyLight),12);TestEqual(TEXT("Block light"),int32(Block.BlockLight),7);
        TestTrue(TEXT("Origin-relative voxel center"),Block.Position.Equals(FVector(-.5,7.5,15.5)));
    }
    TestFalse(TEXT("Duplicate voxel in one batch"),BridgeProtocol::Parse(PacketJson(Text.Replace(TEXT("[[511,0,12,7]]"),TEXT("[[511,0,12,7],[511,0,12,7]]"))),Out));
    TestFalse(TEXT("Index must be inside its 8-cubed cell"),BridgeProtocol::Parse(PacketJson(Text.Replace(TEXT("[[511,0,12,7]]"),TEXT("[[512,0,12,7]]"))),Out));
    TestFalse(TEXT("Palette references are bounded"),BridgeProtocol::Parse(PacketJson(Text.Replace(TEXT("[[511,0,12,7]]"),TEXT("[[511,1,12,7]]"))),Out));
    TestFalse(TEXT("Light integer bounds"),BridgeProtocol::Parse(PacketJson(Text.Replace(TEXT("[[511,0,12,7]]"),TEXT("[[511,0,16,7]]"))),Out));
    TestFalse(TEXT("Boolean light is rejected"),BridgeProtocol::Parse(PacketJson(Text.Replace(TEXT("[[511,0,12,7]]"),TEXT("[[511,0,true,7]]"))),Out));
    TestFalse(TEXT("State path characters rejected"),BridgeProtocol::Parse(PacketJson(Text.Replace(TEXT("facing=east"),TEXT("facing=../east"))),Out));
    auto P=PacketJson(Text);TArray<TSharedPtr<FJsonValue>> Seed;
    for(int32 I=0;I<64;++I) Seed.Add(MakeShared<FJsonValueNumber>(15));
    P->SetArrayField(TEXT("skyTop"),Seed);TestTrue(TEXT("64-column sky boundary"),BridgeProtocol::Parse(P,Out));TestEqual(TEXT("Sky columns retained"),Out.SkyTop.Num(),64);
    P->SetNumberField(TEXT("totalBatches"),2);P->SetNumberField(TEXT("batchIndex"),1);
    TestFalse(TEXT("Sky boundary only on the first atomic batch"),BridgeProtocol::Parse(P,Out));
    P=PacketJson(Text);Seed.Pop();P->SetArrayField(TEXT("skyTop"),Seed);TestFalse(TEXT("Partial sky boundary rejected"),BridgeProtocol::Parse(P,Out));
    P=PacketJson(Text);P->SetArrayField(TEXT("palette"),{});P->SetArrayField(TEXT("blocks"),{});
    TestTrue(TEXT("Entirely empty native cell still commits"),BridgeProtocol::Parse(P,Out));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeVanillaEnvironmentTest,"UEBridge.Protocol.VanillaEnvironment",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeVanillaEnvironmentTest::RunTest(const FString& Parameters) {
    auto P=PacketJson(Input);FBridgePacket Out;
    auto Environment=PacketJson(TEXT("{\"skyFactor\":0.2,\"blockFactor\":1,\"ambient\":0,\"gamma\":0.5,\"nightVision\":0,\"darkness\":0,\"darkenWorld\":0,\"skyColor\":12566463,\"ambientColor\":16777215,\"hasSky\":true}"));
    P->SetObjectField(TEXT("vanillaLight"),Environment);TestTrue(TEXT("Native night/lightmap controls"),BridgeProtocol::Parse(P,Out));TestTrue(TEXT("Environment retained"),Out.VanillaLight.IsValid());
    Environment->SetStringField(TEXT("gamma"),TEXT("0.5"));TestFalse(TEXT("Coerced gamma rejected"),BridgeProtocol::Parse(P,Out));Environment->SetNumberField(TEXT("gamma"),.5);
    Environment->SetNumberField(TEXT("skyFactor"),4.1);TestFalse(TEXT("Sky intensity bound"),BridgeProtocol::Parse(P,Out));Environment->SetNumberField(TEXT("skyFactor"),.2);
    Environment->SetNumberField(TEXT("nightVision"),-1);TestFalse(TEXT("Effect intensity bound"),BridgeProtocol::Parse(P,Out));Environment->SetNumberField(TEXT("nightVision"),0);
    Environment->SetNumberField(TEXT("skyColor"),.5);TestFalse(TEXT("RGB must be integral"),BridgeProtocol::Parse(P,Out));Environment->SetNumberField(TEXT("skyColor"),12566463);
    Environment->SetStringField(TEXT("hasSky"),TEXT("true"));TestFalse(TEXT("Dimension sky flag typed"),BridgeProtocol::Parse(P,Out));Environment->SetBoolField(TEXT("hasSky"),false);
    TestTrue(TEXT("Skyless dimensions supported"),BridgeProtocol::Parse(P,Out));Environment->RemoveField(TEXT("blockFactor"));TestFalse(TEXT("Partial environment rejected"),BridgeProtocol::Parse(P,Out));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeItemTransactionTest,"UEBridge.Protocol.ItemTransactions",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeItemTransactionTest::RunTest(const FString& Parameters) {
    const FString Text=TEXT("{\"v\":1,\"kind\":\"item_drop\",\"session\":\"00000000-0000-4000-8000-000000000001\",\"seq\":5,\"itemTx\":\"00000000-0000-4000-8000-000000000002\",\"itemEpoch\":\"00000000-0000-4000-8000-000000000004\",\"importId\":\"00000000-0000-4000-8000-000000000003\",\"itemId\":\"minecraft:diamond_sword\",\"itemModelKey\":\"minecraft:diamond_sword@aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"itemCount\":1,\"itemMaxCount\":1,\"position\":{\"x\":0,\"y\":1.5,\"z\":1},\"velocity\":{\"x\":0,\"y\":0.2,\"z\":0.3}}");
    FBridgePacket Out;TestTrue(TEXT("Auxiliary item drop without gameplay-event envelope"),BridgeProtocol::Parse(PacketJson(Text),Out));
    TestTrue(TEXT("Ground item model and transaction epoch"),Out.Kind==EBridgeKind::ItemDrop && Out.ItemId==TEXT("minecraft:diamond_sword") && !Out.ItemEpoch.IsEmpty());
    auto P=PacketJson(Text);P->RemoveField(TEXT("itemEpoch"));TestFalse(TEXT("Drop must have inventory session epoch"),BridgeProtocol::Parse(P,Out));
    P=PacketJson(Text);P->SetNumberField(TEXT("itemCount"),2);TestFalse(TEXT("Cannot exceed native maximum stack"),BridgeProtocol::Parse(P,Out));
    P=PacketJson(Text);P->SetStringField(TEXT("itemId"),TEXT("minecraft:stick"));TestFalse(TEXT("Model must correspond to dropped item"),BridgeProtocol::Parse(P,Out));
    P=PacketJson(Text);P->GetObjectField(TEXT("velocity"))->SetNumberField(TEXT("y"),31);TestFalse(TEXT("Velocity components bounded"),BridgeProtocol::Parse(P,Out));
    P=PacketJson(Text);P->GetObjectField(TEXT("position"))->SetStringField(TEXT("x"),TEXT("0"));TestFalse(TEXT("Nested vector types validated"),BridgeProtocol::Parse(P,Out));
    P=PacketJson(Text);P->SetStringField(TEXT("kind"),TEXT("item_resolve"));P->RemoveField(TEXT("itemEpoch"));P->SetNumberField(TEXT("itemRevision"),1);P->SetNumberField(TEXT("itemAccepted"),0);
    TestTrue(TEXT("Pending settlement remains resolvable after the inventory session closes"),BridgeProtocol::Parse(P,Out));
    P->SetNumberField(TEXT("itemAccepted"),.5);TestFalse(TEXT("Accepted inventory count integral"),BridgeProtocol::Parse(P,Out));
    P->SetNumberField(TEXT("itemAccepted"),0);P->SetNumberField(TEXT("itemRevision"),100001);TestFalse(TEXT("Bounded settlement revision"),BridgeProtocol::Parse(P,Out));
    P=PacketJson(Input);P->SetBoolField(TEXT("itemSession"),true);P->SetStringField(TEXT("itemEpoch"),TEXT("not-a-uuid"));TestFalse(TEXT("Input session epoch validated"),BridgeProtocol::Parse(P,Out));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeTemplateSpawnTest,"UEBridge.Protocol.MobTemplateSpawn",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeTemplateSpawnTest::RunTest(const FString& Parameters) {
    auto P=PacketJson(Input);FBridgePacket Out;P->SetStringField(TEXT("kind"),TEXT("event"));P->SetStringField(TEXT("event"),TEXT("mob_template_spawn"));
    P->SetStringField(TEXT("eventId"),TEXT("00000000-0000-4000-8000-000000000002"));P->SetStringField(TEXT("importId"),TEXT("00000000-0000-4000-8000-000000000003"));
    for(const TCHAR* Species:{TEXT("minecraft:zombie"),TEXT("minecraft:villager")}) {
        P->SetStringField(TEXT("mobType"),Species);TestTrue(TEXT("Species template at authoritative feet"),BridgeProtocol::Parse(P,Out));
        TestTrue(TEXT("Template event has exact type and relative position"),Out.Kind==EBridgeKind::MobTemplateSpawn && Out.SpawnType==Species && Out.Position.Equals(FVector(1,2,3)));
    }
    P->SetStringField(TEXT("mobType"),TEXT("minecraft:bad:type"));TestFalse(TEXT("Exactly one namespace delimiter"),BridgeProtocol::Parse(P,Out));
    P->SetStringField(TEXT("mobType"),TEXT("minecraft:Zombie"));TestFalse(TEXT("Uppercase registry identity rejected"),BridgeProtocol::Parse(P,Out));
    P->SetStringField(TEXT("mobType"),TEXT("minecraft:zombie"));P->RemoveField(TEXT("importId"));TestFalse(TEXT("Current world import required"),BridgeProtocol::Parse(P,Out));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeExtendedScopeTest,"UEBridge.Protocol.SixChunkScope",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeExtendedScopeTest::RunTest(const FString& Parameters) {
    auto P=PacketJson(Input);FBridgePacket Out;P->SetStringField(TEXT("kind"),TEXT("event"));P->SetStringField(TEXT("event"),TEXT("world_scope"));
    P->SetStringField(TEXT("eventId"),TEXT("00000000-0000-4000-8000-000000000002"));P->SetNumberField(TEXT("cellX"),0);P->SetNumberField(TEXT("cellY"),0);P->SetNumberField(TEXT("cellZ"),0);
    P->SetNumberField(TEXT("radius"),12);P->SetNumberField(TEXT("halfHeight"),6);TestTrue(TEXT("Six chunk horizontal range and 13 vertical cells"),BridgeProtocol::Parse(P,Out));
    P->SetNumberField(TEXT("halfHeight"),7);TestFalse(TEXT("Height bounded"),BridgeProtocol::Parse(P,Out));
    P->SetStringField(TEXT("event"),TEXT("world_commit"));P->SetStringField(TEXT("importId"),TEXT("00000000-0000-4000-8000-000000000003"));P->SetNumberField(TEXT("cells"),8125);
    TestTrue(TEXT("8125-cell import count"),BridgeProtocol::Parse(P,Out));P->SetNumberField(TEXT("cells"),8126);TestFalse(TEXT("Maximum import count"),BridgeProtocol::Parse(P,Out));
    return true;
}

#endif

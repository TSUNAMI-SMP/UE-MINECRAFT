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
    P->SetNumberField(TEXT("radius"),4); TestFalse(TEXT("World scope limit"),BridgeProtocol::Parse(P,Out));
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
#endif

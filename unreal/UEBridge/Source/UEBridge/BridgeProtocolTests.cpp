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
    TestTrue(TEXT("Axes and 100cm scale"), BridgeProtocol::ToUnreal(P.Position, FVector(10,20,30)).Equals(FVector(310,120,230)));
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
#endif

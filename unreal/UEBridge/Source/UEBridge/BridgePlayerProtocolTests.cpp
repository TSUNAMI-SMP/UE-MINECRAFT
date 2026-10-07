// Execute in UE Session Frontend Automation; UE is not installed in the cloud workspace.
#if WITH_DEV_AUTOMATION_TESTS
#include "BridgeProtocol.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace {
TSharedPtr<FJsonObject> PlayerInputJson() {
    const FString Text=TEXT("{\"v\":1,\"kind\":\"input\",\"session\":\"00000000-0000-4000-8000-000000000001\",\"seq\":12,\"x\":0,\"y\":0,\"z\":0,\"yaw\":90,\"pitch\":0,\"forward\":0,\"right\":0,\"jump\":false}");
    TSharedPtr<FJsonObject> Result;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Result);
    return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgePlayerVisualProtocolTest,"UEBridge.Protocol.PlayerVisuals",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgePlayerVisualProtocolTest::RunTest(const FString& Parameters) {
    auto Json=PlayerInputJson();FBridgePacket Packet;
    TestTrue(TEXT("Legacy input still parses"),BridgeProtocol::Parse(Json,Packet));
    TestEqual(TEXT("Legacy first-person default"),Packet.Perspective,0);
    TestEqual(TEXT("Legacy all outer layers"),Packet.SkinLayers,127);
    TestEqual(TEXT("Legacy equipped hand"),Packet.EquipProgress,1.0);
    TestEqual(TEXT("Missing camera field uses the bridge's 80 degree base"),Packet.CameraFov,80.0);
    TestFalse(TEXT("Legacy classic arms"),Packet.SlimArms);
    TestFalse(TEXT("Legacy cannot acquire flight permission"),Packet.Creative || Packet.Flying);
    Json->SetBoolField(TEXT("creative"),true);Json->SetBoolField(TEXT("flying"),true);
    TestTrue(TEXT("Creative flight parses"),BridgeProtocol::Parse(Json,Packet) && Packet.Flying);
    Json->SetBoolField(TEXT("creative"),false);
    TestTrue(TEXT("Survival input parses but flight is revoked"),BridgeProtocol::Parse(Json,Packet) && !Packet.Flying);
    Json->SetStringField(TEXT("creative"),TEXT("true"));TestFalse(TEXT("String permissions rejected"),BridgeProtocol::Parse(Json,Packet));
    Json->SetBoolField(TEXT("creative"),false);Json->SetBoolField(TEXT("flying"),false);

    Json->SetNumberField(TEXT("perspective"),2);
    Json->SetNumberField(TEXT("skinLayers"),64|4);
    Json->SetBoolField(TEXT("slimArms"),true);
    Json->SetBoolField(TEXT("leftHanded"),true);
    Json->SetNumberField(TEXT("swingProgress"),.25);
    Json->SetNumberField(TEXT("equipProgress"),.75);
    Json->SetNumberField(TEXT("cameraFov"),90);
    Json->SetBoolField(TEXT("usingItem"),true);
    Json->SetStringField(TEXT("useAction"),TEXT("bow"));
    Json->SetNumberField(TEXT("useProgress"),.5);
    TestTrue(TEXT("Minecraft visual field names parse"),BridgeProtocol::Parse(Json,Packet));
    TestEqual(TEXT("Front third-person view"),Packet.Perspective,2);
    TestEqual(TEXT("Outer layer visibility"),Packet.SkinLayers,68);
    TestTrue(TEXT("Skin model and dominant arm"),Packet.SlimArms && Packet.LeftHanded);
    TestEqual(TEXT("Native swing progress"),Packet.SwingProgress,.25);
    TestEqual(TEXT("Native equip progress"),Packet.EquipProgress,.75);
    TestEqual(TEXT("Minecraft vertical field of view"),Packet.CameraFov,90.0);
    TestTrue(TEXT("Use animation metadata"),Packet.UsingItem && Packet.UseAction==TEXT("bow") && Packet.UseProgress==.5);

    Json->SetStringField(TEXT("slimArms"),TEXT("true"));
    TestFalse(TEXT("String skin-model boolean rejected"),BridgeProtocol::Parse(Json,Packet));
    TestTrue(TEXT("Invalid input leaves previous output unchanged"),Packet.SlimArms && Packet.Perspective==2);
    Json->SetBoolField(TEXT("slimArms"),true);
    Json->SetNumberField(TEXT("perspective"),3);
    TestFalse(TEXT("Fourth camera mode rejected"),BridgeProtocol::Parse(Json,Packet));
    Json->SetNumberField(TEXT("perspective"),1.5);
    TestFalse(TEXT("Fractional camera mode rejected"),BridgeProtocol::Parse(Json,Packet));
    Json->SetNumberField(TEXT("perspective"),0);
    Json->SetNumberField(TEXT("skinLayers"),128);
    TestFalse(TEXT("Unknown skin-layer bit rejected"),BridgeProtocol::Parse(Json,Packet));
    Json->SetNumberField(TEXT("skinLayers"),127);
    for(const TCHAR* Field:{TEXT("swingProgress"),TEXT("equipProgress"),TEXT("useProgress")}) {
        Json->SetNumberField(Field,1.01);
        TestFalse(TEXT("Animation progress above one rejected"),BridgeProtocol::Parse(Json,Packet));
        Json->SetStringField(Field,TEXT("0.5"));
        TestFalse(TEXT("String animation progress rejected"),BridgeProtocol::Parse(Json,Packet));
        Json->SetNumberField(Field,.5);
    }
    Json->SetNumberField(TEXT("cameraFov"),111);
    TestFalse(TEXT("Field of view outside Minecraft bounds rejected"),BridgeProtocol::Parse(Json,Packet));
    Json->SetNumberField(TEXT("cameraFov"),70);
    Json->SetStringField(TEXT("useAction"),TEXT("unknown_animation"));
    TestFalse(TEXT("Unsupported use action rejected"),BridgeProtocol::Parse(Json,Packet));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeFeedbackAckProtocolTest,"UEBridge.Protocol.FeedbackAcknowledgment",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeFeedbackAckProtocolTest::RunTest(const FString& Parameters) {
    auto Json=MakeShared<FJsonObject>();FBridgePacket Packet;
    Json->SetNumberField(TEXT("v"),1);
    Json->SetStringField(TEXT("kind"),TEXT("feedback_ack"));
    Json->SetStringField(TEXT("session"),TEXT("00000000-0000-4000-8000-000000000001"));
    Json->SetNumberField(TEXT("seq"),14);
    Json->SetStringField(TEXT("effectId"),TEXT("00000000-0000-4000-8000-000000000002"));
    TestTrue(TEXT("Feedback ACK does not require input pose"),BridgeProtocol::Parse(Json,Packet));
    TestTrue(TEXT("ACK has its own packet kind"),Packet.Kind==EBridgeKind::FeedbackAck);
    TestEqual(TEXT("ACK preserves effect identity"),Packet.EventId,FString(TEXT("00000000-0000-4000-8000-000000000002")));
    Json->SetStringField(TEXT("effectId"),TEXT("not-a-uuid"));
    TestFalse(TEXT("Malformed effect UUID rejected"),BridgeProtocol::Parse(Json,Packet));
    Json->SetStringField(TEXT("effectId"),TEXT("00000000-0000-4000-8000-000000000002"));
    Json->SetStringField(TEXT("session"),TEXT("not-a-session"));
    TestFalse(TEXT("Malformed session UUID rejected"),BridgeProtocol::Parse(Json,Packet));
    Json->SetStringField(TEXT("session"),TEXT("00000000-0000-4000-8000-000000000001"));
    Json->SetNumberField(TEXT("seq"),0);
    TestFalse(TEXT("Invalid packet sequence rejected"),BridgeProtocol::Parse(Json,Packet));
    return true;
}
#endif

package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import java.util.UUID;
import java.util.Locale;
import org.junit.Test;
import static org.junit.Assert.*;

public final class VanillaFeedbackDataTest {
    private static JsonObject packet() {
        JsonObject packet=new JsonObject();
        packet.addProperty("v",1); packet.addProperty("kind","feedback"); packet.addProperty("session",UUID.randomUUID().toString());
        packet.addProperty("receiverId",UUID.randomUUID().toString()); packet.addProperty("effectId",UUID.randomUUID().toString());
        packet.addProperty("seq",12); packet.addProperty("type","break"); packet.addProperty("block","minecraft:stone");
        for (String field:new String[]{"x","y","z","listenerX","listenerY","listenerZ","listenerYaw","listenerPitch"}) packet.addProperty(field,0);
        return packet;
    }
    @Test public void validatesTypesAndCoordinatesBeforeAcknowledgment() {
        JsonObject packet=packet(); assertNotNull(VanillaFeedbackData.parse(packet));
        packet.addProperty("x","3"); assertNull(VanillaFeedbackData.parse(packet));
        packet.addProperty("x",Double.NaN); assertNull(VanillaFeedbackData.parse(packet));
        packet.addProperty("x",100001); assertNull(VanillaFeedbackData.parse(packet));
        packet.addProperty("x",0); packet.addProperty("listenerPitch",91); assertNull(VanillaFeedbackData.parse(packet));
    }
    @Test public void rejectsUnsupportedActionsAndInvalidEventIdentifiers() {
        JsonObject packet=packet(); packet.addProperty("type","click"); assertNull(VanillaFeedbackData.parse(packet));
        packet.addProperty("type","place"); packet.addProperty("effectId","1-2-3-4-5"); assertNull(VanillaFeedbackData.parse(packet));
        packet.addProperty("effectId",UUID.randomUUID().toString()); packet.addProperty("seq",1.5); assertNull(VanillaFeedbackData.parse(packet));
        packet.addProperty("seq",1); packet.addProperty("block","minecraft:Stone"); assertNull(VanillaFeedbackData.parse(packet));
    }
    @Test public void preservesUnrealUppercaseReceiverIdForSessionMatching() {
        JsonObject packet=packet(); String receiver=UUID.randomUUID().toString().toUpperCase(Locale.ROOT);
        packet.addProperty("receiverId",receiver);
        assertEquals(receiver,VanillaFeedbackData.parse(packet).receiverId());
    }
    @Test public void validatesLandingMagnitude() {
        JsonObject packet=packet(); packet.addProperty("type","land"); packet.addProperty("fallDistance",4.5);
        assertEquals(4.5,VanillaFeedbackData.parse(packet).fallDistance(),0);
        packet.addProperty("fallDistance",-1); assertNull(VanillaFeedbackData.parse(packet));
        packet.addProperty("fallDistance",1001); assertNull(VanillaFeedbackData.parse(packet));
    }
    @Test public void shortAndZeroDistanceLandingsRemainAudibleWithoutDamageSoundData() {
        JsonObject packet=packet(); packet.addProperty("type","land");
        for(double distance:new double[]{0,.25,1.25,3,4.5}) {
            packet.addProperty("fallDistance",distance);
            var landing=VanillaFeedbackData.parse(packet);
            assertNotNull(landing); assertEquals(VanillaFeedbackData.Type.LAND,landing.type());
            assertEquals(.5,landing.volume(1),1e-6); assertEquals(.75,landing.pitch(1),1e-6);
        }
    }
    @Test public void preservesListenerRelativeStereoAcrossYawWrapAndFrontView() {
        JsonObject packet=packet(); packet.addProperty("x",-2); packet.addProperty("z",3);
        var local=VanillaFeedbackData.parse(packet).listenerSpace();
        assertEquals(2,local.x(),1e-9); assertEquals(3,local.z(),1e-9);
        packet.addProperty("listenerYaw",180);
        local=VanillaFeedbackData.parse(packet).listenerSpace();
        assertEquals(-2,local.x(),1e-9); assertEquals(-3,local.z(),1e-9);
        packet.addProperty("listenerYaw",540);
        var wrapped=VanillaFeedbackData.parse(packet).listenerSpace();
        assertEquals(local.x(),wrapped.x(),1e-9); assertEquals(local.z(),wrapped.z(),1e-9);
    }
    @Test public void appliesPitchAndRebasesFromAuthoritativeCameraInsteadOfFrozenBody() {
        JsonObject packet=packet(); packet.addProperty("listenerY",5); packet.addProperty("y",3); packet.addProperty("listenerPitch",90);
        var local=VanillaFeedbackData.parse(packet).listenerSpace();
        assertEquals(2,local.z(),1e-9); assertEquals(0,local.y(),1e-9);
    }
    @Test public void usesVanillaVolumeAndPitchForEachEffect() {
        JsonObject packet=packet();
        for (String type:new String[]{"break","place"}) {
            packet.addProperty("type",type); var effect=VanillaFeedbackData.parse(packet);
            assertEquals(.75,effect.volume(.5f),1e-6); assertEquals(.8,effect.pitch(1),1e-6);
        }
        packet.addProperty("type","step"); var effect=VanillaFeedbackData.parse(packet);
        assertEquals(.15,effect.volume(1),1e-6); assertEquals(1,effect.pitch(1),1e-6);
        packet.addProperty("type","land"); effect=VanillaFeedbackData.parse(packet);
        assertEquals(.5,effect.volume(1),1e-6); assertEquals(.75,effect.pitch(1),1e-6);
    }
    @Test public void authoritativeInteractionTypesAreAcceptedAndUnknownActionsRejected() {
        var packet=packet();
        for(String type:new String[]{"open","close","activate","deactivate"}) {
            packet.addProperty("type",type);var result=VanillaFeedbackData.parse(packet);assertNotNull(result);
            assertEquals(type.toUpperCase(Locale.ROOT),result.type().name());assertEquals(1f,result.volume(2f),0);
        }
        packet.addProperty("type","use");assertNull(VanillaFeedbackData.parse(packet));
    }

}

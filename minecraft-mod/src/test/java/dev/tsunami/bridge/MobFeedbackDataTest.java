package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import org.junit.Test;
import static org.junit.Assert.*;

public final class MobFeedbackDataTest {
    private JsonObject packet() {
        JsonObject p=new JsonObject();p.addProperty("type","mob");p.addProperty("sound","minecraft:entity.zombie.hurt");
        p.addProperty("effectId","effect");p.addProperty("session","session");p.addProperty("receiverId","receiver");
        p.addProperty("lx",0);p.addProperty("ly",1.5);p.addProperty("lz",-4);return p;
    }
    @Test public void validNativeSoundAndRelativePosition() { assertTrue(MobFeedbackData.valid(packet())); }
    @Test public void rejectsCoercedStringsAndBooleans() {
        JsonObject p=packet();p.addProperty("lx","1");assertFalse(MobFeedbackData.valid(p));
        p=packet();p.addProperty("effectId",true);assertFalse(MobFeedbackData.valid(p));
    }
    @Test public void rejectsUnboundedNonfinitePositions() {
        JsonObject p=packet();p.addProperty("lz",Double.NaN);assertFalse(MobFeedbackData.valid(p));
        p=packet();p.addProperty("ly",100001);assertFalse(MobFeedbackData.valid(p));
    }
    @Test public void rejectsTraversalAndWrongEffectType() {
        JsonObject p=packet();p.addProperty("sound","minecraft:../secret");assertFalse(MobFeedbackData.valid(p));
        p=packet();p.addProperty("type","break");assertFalse(MobFeedbackData.valid(p));
    }
    @Test public void requiresReceiverIdentityAndAllCoordinates() {
        JsonObject p=packet();p.remove("receiverId");assertFalse(MobFeedbackData.valid(p));
        p=packet();p.remove("ly");assertFalse(MobFeedbackData.valid(p));
    }
}

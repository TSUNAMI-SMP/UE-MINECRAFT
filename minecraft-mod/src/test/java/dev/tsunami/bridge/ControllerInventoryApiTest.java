package dev.tsunami.bridge;

import com.google.gson.JsonParser;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import org.junit.Test;
import static org.junit.Assert.*;

/** Validate the inventory guard against the actual 1.21.11 method and packaged mixin registration. */
public class ControllerInventoryApiTest {
    @Test public void nativeInventoryClickSignatureMatchesGuard() throws Exception {
        ClassLoader loader=getClass().getClassLoader();
        Class<?> manager=Class.forName("net.minecraft.client.network.ClientPlayerInteractionManager",false,loader);
        Class<?> action=Class.forName("net.minecraft.screen.slot.SlotActionType",false,loader);
        Class<?> player=Class.forName("net.minecraft.entity.player.PlayerEntity",false,loader);
        assertEquals(void.class,manager.getDeclaredMethod("clickSlot",int.class,int.class,int.class,action,player).getReturnType());
        Class<?> guard=Class.forName("dev.tsunami.bridge.mixin.ControllerInventoryMixin",false,loader);
        Class<?> callback=Class.forName("org.spongepowered.asm.mixin.injection.callback.CallbackInfo",false,loader);
        assertEquals(void.class,guard.getDeclaredMethod("bridgeNoNativeInventoryDrop",int.class,int.class,int.class,action,player,callback).getReturnType());
    }
    @Test public void inventoryGuardIsRegisteredInDistributedClientConfig() throws Exception {
        try(var input=getClass().getClassLoader().getResourceAsStream("minecraft-ue-bridge.client.mixins.json")) {
            assertNotNull(input);
            var mixins=JsonParser.parseReader(new InputStreamReader(input,StandardCharsets.UTF_8)).getAsJsonObject().getAsJsonArray("client");
            assertTrue(java.util.stream.StreamSupport.stream(mixins.spliterator(),false)
                    .anyMatch(value->"ControllerInventoryMixin".equals(value.getAsString())));
        }
    }
}

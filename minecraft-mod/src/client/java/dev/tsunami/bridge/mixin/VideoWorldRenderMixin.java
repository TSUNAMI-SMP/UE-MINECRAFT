package dev.tsunami.bridge.mixin;

import dev.tsunami.bridge.MinecraftBridgeClient;
import net.minecraft.client.render.GameRenderer;
import net.minecraft.client.render.RenderTickCounter;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** HUD/camera/input stay active; a fresh full-screen UE image replaces native world drawing. */
@Mixin(GameRenderer.class)
public abstract class VideoWorldRenderMixin {
    @Inject(method="renderWorld",at=@At("HEAD"),cancellable=true)
    private void bridgeSkipCoveredWorld(RenderTickCounter ticks,CallbackInfo ci) {
        if(MinecraftBridgeClient.skipWorldRender()) ci.cancel();
    }
}

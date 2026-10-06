package dev.tsunami.bridge;

import net.minecraft.client.MinecraftClient;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(MinecraftClient.class)
public abstract class RenderMixin {
    @Inject(method = "render", at = @At("TAIL"))
    private void bridgeAfterFrame(boolean tick, CallbackInfo ci) {
        MinecraftBridgeClient.renderFrame();
    }
}

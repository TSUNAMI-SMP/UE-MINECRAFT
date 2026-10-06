package dev.tsunami.bridge.mixin;

import dev.tsunami.bridge.MinecraftBridgeClient;
import net.minecraft.client.MinecraftClient;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(MinecraftClient.class)
public abstract class ControllerInteractionMixin {
    @Inject(method="doAttack",at=@At("HEAD"),cancellable=true)
    private void bridgeNoVanillaAttack(CallbackInfoReturnable<Boolean> ci) {
        if(MinecraftBridgeClient.controllerMode()) ci.setReturnValue(false);
    }
    @Inject(method="doItemUse",at=@At("HEAD"),cancellable=true)
    private void bridgeNoVanillaUse(CallbackInfo ci) {
        if(MinecraftBridgeClient.controllerMode()) ci.cancel();
    }
    @Inject(method="handleBlockBreaking",at=@At("HEAD"),cancellable=true)
    private void bridgeNoVanillaBreak(boolean breaking,CallbackInfo ci) {
        if(MinecraftBridgeClient.controllerMode()) ci.cancel();
    }
}

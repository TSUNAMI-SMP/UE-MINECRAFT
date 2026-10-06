package dev.tsunami.bridge.mixin;

import dev.tsunami.bridge.MinecraftBridgeClient;
import net.minecraft.client.network.ClientPlayerEntity;
import net.minecraft.util.math.Vec3d;
import net.minecraft.item.ItemStack;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(ClientPlayerEntity.class)
public abstract class ControllerPlayerMixin {
    @Inject(method="tickMovement",at=@At("HEAD"),cancellable=true)
    private void bridgeControllerMovement(CallbackInfo ci) {
        if(MinecraftBridgeClient.controllerMode()) {
            ((ClientPlayerEntity)(Object)this).setVelocity(Vec3d.ZERO);
            ci.cancel();
        }
    }
    @Inject(method="dropSelectedItem",at=@At("HEAD"),cancellable=true)
    private void bridgeNoVanillaDrop(boolean entireStack,CallbackInfoReturnable<Boolean> ci) {
        if(MinecraftBridgeClient.controllerMode()) ci.setReturnValue(false);
    }
    @Inject(method="dropCreativeStack",at=@At("HEAD"),cancellable=true)
    private void bridgeNoCreativeDrop(ItemStack stack,CallbackInfo ci) {
        if(MinecraftBridgeClient.controllerMode()) ci.cancel();
    }
}

package dev.tsunami.bridge.mixin;

import dev.tsunami.bridge.MinecraftBridgeClient;

import net.minecraft.entity.LivingEntity;
import net.minecraft.item.BowItem;
import net.minecraft.item.ItemStack;
import net.minecraft.world.World;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(BowItem.class)
public abstract class BowMixin {
    @Inject(method = "onStoppedUsing", at = @At("RETURN"))
    private void bridgeBowRelease(ItemStack stack, World world, LivingEntity user, int remaining,
                                 CallbackInfoReturnable<Boolean> result) {
        if (world.isClient() && result.getReturnValue()) {
            BowItem item = (BowItem) (Object) this;
            MinecraftBridgeClient.bowReleased(user, BowItem.getPullProgress(item.getMaxUseTime(stack, user) - remaining));
        }
    }
}

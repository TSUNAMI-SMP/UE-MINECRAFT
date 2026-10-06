package dev.tsunami.bridge.mixin;

import dev.tsunami.bridge.MobAuthorityLease;
import net.minecraft.entity.Entity;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.server.world.ServerWorld;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ServerWorld.class)
public abstract class MobAuthorityMixin {
    // Intercept the outer world tick, before any species override (creeper fuse etc.) runs.
    @Inject(method = "tickEntity", at = @At("HEAD"), cancellable = true)
    private void bridgePauseImportedSourceMob(Entity entity, CallbackInfo info) {
        if (entity instanceof MobEntity mob && MobAuthorityLease.suspended(mob)) info.cancel();
    }
}

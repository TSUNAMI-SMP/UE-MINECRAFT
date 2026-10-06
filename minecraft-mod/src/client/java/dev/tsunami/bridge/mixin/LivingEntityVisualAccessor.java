package dev.tsunami.bridge.mixin;

import net.minecraft.entity.LivingEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

/** Advance only the hand animation that freezing native player movement otherwise skips. */
@Mixin(LivingEntity.class)
public interface LivingEntityVisualAccessor {
    @Invoker("tickHandSwing") void bridgeTickHandSwing();
}

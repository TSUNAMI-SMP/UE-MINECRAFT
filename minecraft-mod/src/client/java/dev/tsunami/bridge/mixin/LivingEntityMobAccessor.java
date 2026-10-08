package dev.tsunami.bridge.mixin;

import net.minecraft.client.render.entity.LivingEntityRenderer;
import net.minecraft.client.render.entity.state.LivingEntityRenderState;
import net.minecraft.client.util.math.MatrixStack;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;
import org.spongepowered.asm.mixin.gen.Accessor;

/** Captures the renderer's species-specific scale (giants/slimes etc.). */
@Mixin(LivingEntityRenderer.class)
public interface LivingEntityMobAccessor {
    @Invoker("scale") void bridgeMobScale(LivingEntityRenderState state, MatrixStack matrices);
    @Accessor("features") java.util.List<?> bridgeFeatures();
}

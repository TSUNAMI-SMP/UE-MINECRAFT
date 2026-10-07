package dev.tsunami.bridge.mixin;

import net.minecraft.client.render.LightmapTextureManager;
import net.minecraft.entity.LivingEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
import org.spongepowered.asm.mixin.gen.Invoker;

/** Reuses the active vanilla lightmap flicker and darkness; no texture is exported. */
@Mixin(LightmapTextureManager.class)
public interface LightmapStateAccessor {
    @Accessor("flickerIntensity") float bridge$getFlickerIntensity();
    @Invoker("getDarkness") float bridge$getDarkness(LivingEntity entity,float factor,float tickDelta);
}

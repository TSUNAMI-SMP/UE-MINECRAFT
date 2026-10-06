package dev.tsunami.bridge.mixin;

import net.minecraft.client.render.entity.AgeableMobEntityRenderer;
import net.minecraft.client.render.entity.model.EntityModel;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(AgeableMobEntityRenderer.class)
public interface AgeableMobModelAccessor {
    @Accessor("adultModel") EntityModel<?> bridgeAdultModel();
    @Accessor("babyModel") EntityModel<?> bridgeBabyModel();
}

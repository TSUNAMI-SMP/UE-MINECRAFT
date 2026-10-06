package dev.tsunami.bridge.mixin;

import java.util.List;
import java.util.Map;
import net.minecraft.client.model.ModelPart;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** Reads the already baked, active resource-pack entity geometry; never modifies a model. */
@Mixin(ModelPart.class)
public interface ModelPartMobAccessor {
    @Accessor("cuboids") List<ModelPart.Cuboid> bridgeMobCuboids();
    @Accessor("children") Map<String, ModelPart> bridgeMobChildren();
}

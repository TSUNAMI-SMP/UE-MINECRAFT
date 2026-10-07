package dev.tsunami.bridge.mixin;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
@Mixin(net.minecraft.client.render.RenderLayer.class)
public interface ItemLayerTexturesAccessor { @Accessor("renderSetup") net.minecraft.client.render.RenderSetup bridgeSetup(); }

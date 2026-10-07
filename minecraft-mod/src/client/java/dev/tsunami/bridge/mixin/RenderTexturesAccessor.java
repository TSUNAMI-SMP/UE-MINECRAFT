package dev.tsunami.bridge.mixin;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
@Mixin(net.minecraft.client.render.RenderSetup.class)
public interface RenderTexturesAccessor { @Accessor("textures") java.util.Map<String, ?> bridgeTextures(); }

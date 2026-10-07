package dev.tsunami.bridge.mixin;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
@Mixin(net.minecraft.client.texture.SpriteContents.class)
public interface SpriteImageAccessor { @Accessor("image") net.minecraft.client.texture.NativeImage bridgeImage(); }

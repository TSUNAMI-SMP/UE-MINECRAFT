package dev.tsunami.bridge.mixin;
import net.minecraft.block.FenceGateBlock;
import net.minecraft.block.WoodType;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
@Mixin(FenceGateBlock.class)
public interface GateSoundAccessor { @Accessor("type") WoodType bridgeSoundType(); }

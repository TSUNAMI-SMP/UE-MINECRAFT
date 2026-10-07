package dev.tsunami.bridge.mixin;
import net.minecraft.block.TrapdoorBlock;
import net.minecraft.block.BlockSetType;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
@Mixin(TrapdoorBlock.class)
public interface TrapdoorSoundAccessor { @Accessor("blockSetType") BlockSetType bridgeSoundType(); }

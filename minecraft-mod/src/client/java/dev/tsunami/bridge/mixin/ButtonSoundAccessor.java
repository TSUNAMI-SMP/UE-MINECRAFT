package dev.tsunami.bridge.mixin;
import net.minecraft.block.ButtonBlock;
import net.minecraft.block.BlockSetType;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
@Mixin(ButtonBlock.class)
public interface ButtonSoundAccessor { @Accessor("blockSetType") BlockSetType bridgeSoundType(); }

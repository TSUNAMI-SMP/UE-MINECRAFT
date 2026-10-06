package dev.tsunami.bridge.mixin;

import net.minecraft.client.render.item.HeldItemRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** Read the equip animation Minecraft already ticks, even while UE supplies the world image. */
@Mixin(HeldItemRenderer.class)
public interface HeldItemRendererAccessor {
    @Accessor("equipProgressMainHand") float bridgeEquipMain();
    @Accessor("lastEquipProgressMainHand") float bridgeLastEquipMain();
    @Accessor("equipProgressOffHand") float bridgeEquipOff();
    @Accessor("lastEquipProgressOffHand") float bridgeLastEquipOff();
}

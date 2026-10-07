package dev.tsunami.bridge.mixin;

import dev.tsunami.bridge.MinecraftBridgeClient;
import net.minecraft.client.network.ClientPlayerInteractionManager;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.screen.slot.SlotActionType;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Inventory-screen throws bypass the main-hand drop hook. Cancel before vanilla prediction/network mutation. */
@Mixin(ClientPlayerInteractionManager.class)
public abstract class ControllerInventoryMixin {
    @Inject(method="clickSlot",at=@At("HEAD"),cancellable=true)
    private void bridgeNoNativeInventoryDrop(int syncId,int slotId,int button,SlotActionType action,
                                             PlayerEntity player,CallbackInfo callback) {
        // -999/PICKUP is vanilla's drag-and-drop outside the inventory window.
        if(MinecraftBridgeClient.controllerMode() && (action==SlotActionType.THROW || (slotId==-999 && action==SlotActionType.PICKUP))) {
            MinecraftBridgeClient.inventoryDropBlocked();callback.cancel();
        }
    }
}

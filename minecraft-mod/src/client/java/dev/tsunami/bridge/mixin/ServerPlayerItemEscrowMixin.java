package dev.tsunami.bridge.mixin;

import dev.tsunami.bridge.ItemDropBridge;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.storage.ReadView;
import net.minecraft.storage.WriteView;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ServerPlayerEntity.class)
public abstract class ServerPlayerItemEscrowMixin {
    @Inject(method="writeCustomData",at=@At("TAIL"))
    private void bridgeSaveEscrow(WriteView view,CallbackInfo callback) {ItemDropBridge.writeEscrow((ServerPlayerEntity)(Object)this,view);}
    @Inject(method="readCustomData",at=@At("TAIL"))
    private void bridgeLoadEscrow(ReadView view,CallbackInfo callback) {ItemDropBridge.readEscrow((ServerPlayerEntity)(Object)this,view);}
}

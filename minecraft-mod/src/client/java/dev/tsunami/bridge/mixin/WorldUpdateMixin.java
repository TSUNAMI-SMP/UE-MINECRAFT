package dev.tsunami.bridge.mixin;

import dev.tsunami.bridge.MinecraftBridgeClient;
import net.minecraft.block.BlockState;
import net.minecraft.client.world.ClientWorld;
import net.minecraft.util.math.BlockPos;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(ClientWorld.class)
public abstract class WorldUpdateMixin {
    @Inject(method = "setBlockState", at = @At("RETURN"))
    private void bridgeBlockChanged(BlockPos pos, BlockState state, int flags, int depth, CallbackInfoReturnable<Boolean> result) {
        if (result.getReturnValue()) MinecraftBridgeClient.blockChanged(pos);
    }
}

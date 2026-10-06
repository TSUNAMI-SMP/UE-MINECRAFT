package dev.tsunami.bridge.mixin;

import dev.tsunami.bridge.VanillaSkyComposite;
import net.minecraft.client.gui.hud.InGameOverlayRenderer;
import net.minecraft.client.render.command.OrderedRenderCommandQueue;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Frozen Minecraft terrain must never produce block/fire/water overlays over UE video. */
@Mixin(InGameOverlayRenderer.class)
public abstract class VanillaSkyOverlayMixin {
    @Inject(method="renderOverlays",at=@At("HEAD"),cancellable=true)
    private void bridgeSkyNoFrozenOverlays(boolean sleeping,float progress,OrderedRenderCommandQueue commands,CallbackInfo ci) {
        if(VanillaSkyComposite.active()) ci.cancel();
    }
}

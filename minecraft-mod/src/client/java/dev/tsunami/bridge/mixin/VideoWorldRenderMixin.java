package dev.tsunami.bridge.mixin;

import dev.tsunami.bridge.MinecraftBridgeClient;
import dev.tsunami.bridge.VanillaSkyComposite;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.render.Camera;
import net.minecraft.client.render.GameRenderer;
import net.minecraft.client.render.RenderTickCounter;
import net.minecraft.client.util.math.MatrixStack;
import net.minecraft.util.math.Vec3d;
import org.joml.Matrix4f;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import net.minecraft.util.math.MathHelper;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** HUD/camera/input stay active; a fresh full-screen UE image replaces native world drawing. */
@Mixin(GameRenderer.class)
public abstract class VideoWorldRenderMixin {
    @Shadow @Final private Camera camera;
    @Shadow @Final private MinecraftClient client;
    @Inject(method="updateCamera",at=@At("TAIL"))
    private void bridgeCapturedSkyCamera(RenderTickCounter ticks,CallbackInfo ci) {
        VanillaSkyComposite.prepare(MinecraftBridgeClient.videoClient(),MinecraftBridgeClient.vanillaSkyRender());
        var view=VanillaSkyComposite.camera();
        if(view!=null && client.world!=null) {
            var pos=new Vec3d(view.x(),view.y(),view.z());
            var mutable=(BridgeCameraAccessor)(Object)camera;
            mutable.bridgeSetPos(pos);mutable.bridgeSetRotation(view.yaw(),view.pitch());
            camera.getEnvironmentAttributeInterpolator().update(client.world,pos);
        }
    }
    @Inject(method="renderWorld",at=@At("HEAD"),cancellable=true)
    private void bridgeSkipCoveredWorld(RenderTickCounter ticks,CallbackInfo ci) {
        if(MinecraftBridgeClient.skipWorldRender()) ci.cancel();
    }
    @Inject(method="getFov",at=@At("HEAD"),cancellable=true)
    private void bridgeSkyFov(Camera camera,float progress,boolean changing,CallbackInfoReturnable<Float> ci) {
        if(VanillaSkyComposite.active()) ci.setReturnValue(VanillaSkyComposite.projectionFov(client.getWindow().getFramebufferWidth(),client.getWindow().getFramebufferHeight()));
    }
    @Inject(method="renderHand",at=@At("HEAD"),cancellable=true)
    private void bridgeSkipNativeHand(float progress,boolean sleeping,Matrix4f matrix,CallbackInfo ci) {
        if(VanillaSkyComposite.active()) ci.cancel();
    }
    @Inject(method={"bobView","tiltViewWhenHurt"},at=@At("HEAD"),cancellable=true)
    private void bridgeSkyNoNativeBob(MatrixStack matrices,float progress,CallbackInfo ci) {
        if(VanillaSkyComposite.active()) ci.cancel();
    }
    @Redirect(method="renderWorld",at=@At(value="INVOKE",target="Lnet/minecraft/util/math/MathHelper;lerp(FFF)F"))
    private float bridgeSkyNoFrozenPortalDistortion(float delta,float start,float end) {
        return VanillaSkyComposite.active() ? 0 : MathHelper.lerp(delta,start,end);
    }
}

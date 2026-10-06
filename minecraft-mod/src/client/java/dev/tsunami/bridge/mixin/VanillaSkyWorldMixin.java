package dev.tsunami.bridge.mixin;

import com.mojang.blaze3d.buffers.GpuBufferSlice;
import com.mojang.blaze3d.systems.RenderSystem;
import dev.tsunami.bridge.VanillaSkyComposite;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.option.CloudRenderMode;
import net.minecraft.client.render.*;
import net.minecraft.client.render.state.WorldRenderState;
import net.minecraft.client.util.memory.ObjectAllocator;
import net.minecraft.client.world.ClientWorld;
import net.minecraft.util.math.ColorHelper;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.attribute.EnvironmentAttributes;
import org.joml.Matrix4f;
import org.joml.Vector4f;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Native1.21.11 sky/cloud graph, without native terrain, entities, weather geometry or particles. */
@Mixin(WorldRenderer.class)
public abstract class VanillaSkyWorldMixin {
    @Shadow @Final private MinecraftClient client;
    @Shadow private ClientWorld world;
    @Shadow private SkyRendering skyRendering;
    @Shadow @Final private DefaultFramebufferSet framebufferSet;
    @Shadow @Final private WorldRenderState worldRenderState;
    @Shadow private void renderSky(FrameGraphBuilder graph,Camera camera,GpuBufferSlice fog) {throw new AssertionError();}
    @Shadow private void renderClouds(FrameGraphBuilder graph,CloudRenderMode mode,Vec3d pos,long time,float progress,int color,float height) {throw new AssertionError();}

    @Inject(method="render",at=@At("HEAD"),cancellable=true)
    private void bridgeOnlyNativeSky(ObjectAllocator allocator,RenderTickCounter ticks,boolean outline,Camera camera,
        Matrix4f view,Matrix4f projection,Matrix4f cullingProjection,GpuBufferSlice fog,Vector4f fogColor,boolean sky,CallbackInfo ci) {
        if(!VanillaSkyComposite.active()) return;
        if(world==null || skyRendering==null) {
            var target=client.getFramebuffer();
            RenderSystem.getDevice().createCommandEncoder().clearColorAndDepthTextures(target.getColorAttachment(),
                ColorHelper.fromFloats(0,fogColor.x,fogColor.y,fogColor.z),target.getDepthAttachment(),1);
            ci.cancel();return;
        }
        float progress=ticks.getTickProgress(false);
        worldRenderState.time=world.getTime();
        skyRendering.updateRenderState(world,progress,camera,worldRenderState.skyRenderState);
        // Vanilla's helper checks the local player's frozen eye; the bridge sky must use its captured camera height.
        worldRenderState.skyRenderState.shouldRenderSkyDark=camera.getCameraPos().y<world.getLevelProperties().getSkyDarknessHeight(world);
        var stack=RenderSystem.getModelViewStack();stack.pushMatrix();stack.mul(view);
        var graph=new FrameGraphBuilder();
        framebufferSet.clear();
        framebufferSet.mainFramebuffer=graph.createObjectNode("main",client.getFramebuffer());
        var clear=graph.createPass("bridge_sky_clear");
        framebufferSet.mainFramebuffer=clear.transfer(framebufferSet.mainFramebuffer);
        clear.setRenderer(() -> {
            var target=client.getFramebuffer();
            RenderSystem.getDevice().createCommandEncoder().clearColorAndDepthTextures(target.getColorAttachment(),
                ColorHelper.fromFloats(0,fogColor.x,fogColor.y,fogColor.z),target.getDepthAttachment(),1);
        });
        // Native sky reads native time, moon phase, weather and resource-pack sky assets.
        if(sky) renderSky(graph,camera,fog);
        var mode=client.options.getCloudRenderModeValue();
        if(mode!=CloudRenderMode.OFF) {
            var attributes=camera.getEnvironmentAttributeInterpolator();
            int color=attributes.get(EnvironmentAttributes.CLOUD_COLOR_VISUAL,progress);
            if(ColorHelper.getAlpha(color)>0) renderClouds(graph,mode,camera.getCameraPos(),worldRenderState.time,progress,color,
                attributes.get(EnvironmentAttributes.CLOUD_HEIGHT_VISUAL,progress));
        }
        try {graph.run(allocator);} finally {framebufferSet.clear();stack.popMatrix();worldRenderState.clear();}
        ci.cancel();
    }
}

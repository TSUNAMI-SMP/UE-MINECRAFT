package dev.tsunami.bridge.mixin;

import net.minecraft.client.render.Camera;
import net.minecraft.util.math.Vec3d;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(Camera.class)
public interface BridgeCameraAccessor {
    @Invoker("setRotation") void bridgeSetRotation(float yaw,float pitch);
    @Invoker("setPos") void bridgeSetPos(Vec3d pos);
}

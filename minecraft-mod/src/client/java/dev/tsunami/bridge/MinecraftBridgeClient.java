package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientLifecycleEvents;
import net.fabricmc.fabric.api.event.player.UseBlockCallback;
import net.minecraft.block.Blocks;
import net.minecraft.client.MinecraftClient;
import net.minecraft.item.Items;
import net.minecraft.util.ActionResult;
import net.minecraft.util.math.Vec3d;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import java.io.IOException;

public final class MinecraftBridgeClient implements ClientModInitializer {
    private static MinecraftBridgeClient instance;
    public static void renderFrame() { if (instance != null) instance.frame(); }
    private static final Logger LOG = LoggerFactory.getLogger("minecraft-ue-bridge");
    private BridgeTransport transport;
    private Object world;
    private Vec3d origin;
    private long lastFrame, lastError;
    @Override public void onInitializeClient() {
        instance = this;
        UseBlockCallback.EVENT.register((player, world, hand, hit) -> {
            // Callback runs before vanilla: PASS keeps normal TNT ignition unchanged.
            // Only the local client, an actual TNT block, and ignition tools qualify.
            MinecraftClient mc = MinecraftClient.getInstance();
            if (world.isClient() && player == mc.player && transport != null && origin != null
                    && !player.isSpectator() && !player.isSneaking()
                    && world.getBlockState(hit.getBlockPos()).isOf(Blocks.TNT)
                    && (player.getStackInHand(hand).isOf(Items.FLINT_AND_STEEL)
                        || player.getStackInHand(hand).isOf(Items.FIRE_CHARGE))) {
                // Confirm the TNT block was removed by vanilla on the next client tick.
                pendingIgnition = hit.getBlockPos().toImmutable();
                pendingWorld = world;
                ignitionTicks = 0;
            }
            return ActionResult.PASS;
        });
        net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents.END_CLIENT_TICK.register(mc -> {
            if (pendingIgnition == null) return;
            if (mc.world != pendingWorld || ++ignitionTicks > 10) { pendingIgnition = null; return; }
            if (!mc.world.getBlockState(pendingIgnition).isOf(Blocks.TNT)) {
                try {
                    JsonObject event = transport.packet("event"); event.addProperty("event", "tnt_ignite");
                    position(event, Vec3d.ofCenter(pendingIgnition)); transport.event(event);
                } catch (IOException e) { report(e); }
                pendingIgnition = null;
            }
        });
        ClientLifecycleEvents.CLIENT_STOPPING.register(mc -> disconnect());
    }
    private net.minecraft.util.math.BlockPos pendingIgnition;
    private Object pendingWorld;
    private int ignitionTicks;
    private void frame() {
        MinecraftClient mc = MinecraftClient.getInstance();
        if (mc.player == null || mc.world == null) { disconnect(); world = null; return; }
        if (world != mc.world) {
            disconnect(); world = mc.world; origin = new Vec3d(mc.player.getX(), mc.player.getY(), mc.player.getZ()); pendingIgnition = null;
            try { transport = new BridgeTransport(7779); }
            catch (IOException e) { report(e); return; }
        }
        if (transport == null) return;
        long now = System.nanoTime(); if (now - lastFrame < 8_333_333L) return;
        lastFrame = now;
        try {
            transport.pump();
            JsonObject p = transport.packet("input"); position(p, new Vec3d(mc.player.getX(), mc.player.getY(), mc.player.getZ()));
            p.addProperty("yaw", mc.player.getYaw()); p.addProperty("pitch", mc.player.getPitch());
            boolean active = mc.currentScreen == null && !mc.isPaused();
            p.addProperty("forward", active ? axis(mc.options.forwardKey.isPressed(), mc.options.backKey.isPressed()) : 0);
            p.addProperty("right", active ? axis(mc.options.rightKey.isPressed(), mc.options.leftKey.isPressed()) : 0);
            p.addProperty("jump", active && mc.options.jumpKey.isPressed());
            transport.input(p);
        } catch (IOException e) { report(e); }
    }
    private static int axis(boolean positive, boolean negative) { return (positive ? 1 : 0) - (negative ? 1 : 0); }
    private void position(JsonObject p, Vec3d pos) {
        p.addProperty("x", pos.x - origin.x); p.addProperty("y", pos.y - origin.y); p.addProperty("z", pos.z - origin.z);
    }
    private void report(IOException e) {
        long now = System.nanoTime();
        if (now - lastError > 5_000_000_000L) { LOG.warn("UE bridge: {}", e.toString()); lastError = now; }
    }
    private void disconnect() {
        if (transport != null) try { transport.close(); } catch (IOException e) { report(e); }
        transport = null; origin = null; pendingIgnition = null;
    }
}

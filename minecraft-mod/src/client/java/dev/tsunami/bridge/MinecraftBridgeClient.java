package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import com.mojang.brigadier.arguments.IntegerArgumentType;
import com.mojang.brigadier.arguments.DoubleArgumentType;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.command.v2.ClientCommandRegistrationCallback;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientEntityEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientLifecycleEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.event.player.UseBlockCallback;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.block.Blocks;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.world.ClientWorld;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.TntEntity;
import net.minecraft.item.Items;
import net.minecraft.text.Text;
import net.minecraft.util.ActionResult;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec3d;
import net.minecraft.util.Identifier;
import net.minecraft.registry.Registries;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.function.Consumer;
import static net.fabricmc.fabric.api.client.command.v2.ClientCommandManager.*;

public final class MinecraftBridgeClient implements ClientModInitializer {
    private static MinecraftBridgeClient instance;
    public static void renderFrame() { if (instance != null) instance.frame(); }
    public static void bowReleased(LivingEntity user, float pull) {
        if (instance != null) instance.bow(user, pull);
    }
    public static void blockChanged(BlockPos pos) {
        if (instance != null && instance.config.worldSync) instance.worldSync.changed(pos);
    }
    private static final Logger LOG = LoggerFactory.getLogger("minecraft-ue-bridge");
    private final IgnitionTracker ignition = new IgnitionTracker();
    private final WorldSync worldSync = new WorldSync();
    private final VideoOverlay videoOverlay = new VideoOverlay();
    private VideoClient video;
    private final ArrayDeque<JsonObject> snapshotQueue = new ArrayDeque<>();
    private final Path configPath = FabricLoader.getInstance().getConfigDir().resolve("minecraft-ue-bridge.json");
    private BridgeConfig config = new BridgeConfig();
    private String configError;
    private BridgeTransport transport;
    private ClientWorld world;
    private Vec3d origin;
    private long lastFrame, lastError, lastRetry, reportedExpired, snapshotSequence;
    private boolean lastConnected;
    private String lastReceiverId = "";
    private String lastVideoConfig = "";
    private TextureExportJob textureJob;

    @Override public void onInitializeClient() {
        instance = this; reload(); commands(); videoOverlay.register();
        UseBlockCallback.EVENT.register((player, w, hand, hit) -> {
            MinecraftClient mc = MinecraftClient.getInstance();
            if (w.isClient() && w == world && player == mc.player && transport != null && origin != null
                    && !player.isSpectator() && !player.isSneaking()
                    && w.getBlockState(hit.getBlockPos()).isOf(Blocks.TNT)
                    && (player.getStackInHand(hand).isOf(Items.FLINT_AND_STEEL)
                        || player.getStackInHand(hand).isOf(Items.FIRE_CHARGE))) {
                BlockPos p = hit.getBlockPos(); ignition.click(p.getX(), p.getY(), p.getZ(), System.nanoTime());
            }
            return ActionResult.PASS; // Never intercept vanilla interaction or mutate world/server state.
        });
        ClientEntityEvents.ENTITY_LOAD.register((entity, w) -> {
            if (w == world && entity instanceof TntEntity && transport != null && origin != null) {
                var p = ignition.primed(entity.getX(), entity.getY(), entity.getZ(), System.nanoTime());
                if (p != null) sendEvent("tnt_ignite", new Vec3d(p.x() + 0.5, p.y() + 0.5, p.z() + 0.5), null);
            }
        });
        ClientTickEvents.END_CLIENT_TICK.register(mc -> tick(mc));
        ClientLifecycleEvents.CLIENT_STOPPING.register(mc -> { disconnect(); if(textureJob!=null) textureJob.close(); });
    }
    private void reload() {
        disconnect();
        try {
            config = BridgeConfig.load(configPath); configError = null;
            if (!java.nio.file.Files.exists(configPath)) config.save(configPath);
        } catch (IOException e) { configError = e.getMessage(); config.enabled = false; report(e); }
    }
    private void ensureConnection(MinecraftClient mc) {
        if (world != mc.world) { disconnect(); world = mc.world; }
        if (!config.enabled || mc.player == null || mc.world == null) { disconnect(); return; }
        if (transport == null && (lastRetry == 0 || System.nanoTime() - lastRetry >= 1_000_000_000L)) {
            lastRetry = System.nanoTime();
            try {
                transport = new BridgeTransport(config.port);
                origin = new Vec3d(mc.player.getX(), mc.player.getY(), mc.player.getZ());
                reportedExpired = 0;
            } catch (IOException e) { report(e); }
        }
    }
    private void frame() {
        MinecraftClient mc = MinecraftClient.getInstance(); ensureConnection(mc);
        if (transport == null) return;
        long now = System.nanoTime(); if (lastFrame != 0 && now - lastFrame < 1_000_000_000L / config.inputHz) return;
        lastFrame = now;
        try {
            transport.pump();
            JsonObject p = transport.packet("input");
            position(p, new Vec3d(mc.player.getX(), mc.player.getY(), mc.player.getZ()));
            p.addProperty("yaw", mc.player.getYaw()); p.addProperty("pitch", mc.player.getPitch());
            boolean active = mc.currentScreen == null && !mc.isPaused();
            p.addProperty("forward", active ? axis(mc.options.forwardKey.isPressed(), mc.options.backKey.isPressed()) : 0);
            p.addProperty("right", active ? axis(mc.options.rightKey.isPressed(), mc.options.leftKey.isPressed()) : 0);
            p.addProperty("jump", active && mc.options.jumpKey.isPressed());
            p.addProperty("sneak", mc.player.isSneaking());
            p.addProperty("eyeHeight", mc.player.getEyeHeight(mc.player.getPose()));
            p.addProperty("bodyHeight", mc.player.getHeight()); transport.input(p);
        } catch (IOException e) { report(e); }
    }
    private void tick(MinecraftClient mc) {
        ensureConnection(mc); if (transport == null) return;
        try {
            transport.pump();
            var ready=transport.diagnostics();
            if (ready.connected() && (!lastConnected || !ready.receiverId().equals(lastReceiverId))) {
                lastReceiverId=ready.receiverId(); lastVideoConfig=""; worldSync.reset();
            }
            if (config.worldSync) worldSync.tick(mc, origin, config, transport);
            if (config.videoMode != 0 && video == null && transport.diagnostics().cameraReady() && transport.diagnostics().videoSupported())
                video = new VideoClient(config.videoPort, transport.session());
            if (config.videoMode == 0 && video != null) { video.close(); video = null; }
            videoOverlay.setClient(video, config.videoMode);
            String videoSettings=config.videoQuality+":"+config.videoExposure;
            if(config.videoMode!=0 && ready.videoControlsSupported() && !videoSettings.equals(lastVideoConfig) && transport.availableEvents()>0) {
                int[][] presets={{480,270,15,75},{960,540,20,85},{1280,720,30,90}}; int[] preset=presets[config.videoQuality];
                JsonObject p=transport.packet("event"); p.addProperty("event","video_config"); position(p,origin);
                p.addProperty("width",preset[0]); p.addProperty("height",preset[1]); p.addProperty("fps",preset[2]);
                p.addProperty("quality",preset[3]); p.addProperty("exposure",config.videoExposure);
                transport.event(p); lastVideoConfig=videoSettings;
            }
            // Leave 16 slots for gameplay events. Bulk work never blocks the input path.
            for (int i = 0; i < 4 && !snapshotQueue.isEmpty() && transport.availableEvents() > 16; i++) {
                JsonObject payload = snapshotQueue.removeFirst(); JsonObject p = transport.packet("event");
                if (snapshotSequence == 0) snapshotSequence = p.get("seq").getAsLong();
                payload.addProperty("snapshotSeq", snapshotSequence);
                payload.entrySet().forEach(e -> p.add(e.getKey(), e.getValue()));
                position(p, origin); transport.event(p);
            }
            var d = transport.diagnostics();
            if (d.connected() != lastConnected) {
                lastConnected = d.connected();
                if (config.notifications) notifyPlayer(d.connected()
                        ? ("diagnostic".equals(d.receiver()) ? "UE Bridge: UDP診断ツールへ接続しました" : "UE Bridge: 接続を確認しました")
                        : "UE Bridge: 受信側の応答が途絶えました");
            }
            if (d.expired() > reportedExpired) {
                lastVideoConfig="";
                reportedExpired = d.expired();
                LOG.warn("UE Bridge: {} event(s) expired without ACK", reportedExpired);
                if (config.notifications) notifyPlayer("UE Bridge: 未確認イベントあり。/uebridge status で確認してください");
            }
        } catch (IOException e) { report(e); }
    }
    private void bow(LivingEntity user, float pull) {
        if (!config.bowEvents || user != MinecraftClient.getInstance().player || transport == null || origin == null) return;
        sendEvent("bow_fire", user.getEyePos(), p -> {
            Vec3d direction = user.getRotationVec(1);
            p.addProperty("dx", direction.x); p.addProperty("dy", direction.y); p.addProperty("dz", direction.z);
            p.addProperty("pull", pull);
        });
    }
    private void sendEvent(String name, Vec3d pos, Consumer<JsonObject> extra) {
        try {
            JsonObject p = transport.packet("event"); p.addProperty("event", name); position(p, pos);
            if (extra != null) extra.accept(p); transport.event(p);
        } catch (IOException e) { report(e); }
    }
    private int snapshot() {
        if (transport == null || origin == null) return feedback("先にワールドへ入り、Bridgeを有効にしてください");
        if (!transport.diagnostics().connected()) return feedback("UE 0.2.0の応答を確認してから再実行してください。/uebridge status");
        if (!transport.diagnostics().cameraReady()) return feedback("UEカメラが準備できていません。GameMode / Characterを確認してください");
        if (!snapshotQueue.isEmpty() || transport.diagnostics().pending() != 0) return feedback("前のイベント送信が完了してから再実行してください");
        MinecraftClient mc = MinecraftClient.getInstance(); BlockPos center = mc.player.getBlockPos();
        ArrayList<BlockSnapshot.Block> blocks = new ArrayList<>();
        int r = config.previewRadius, h = config.previewHalfHeight;
        for (int x = -r; x <= r; x++) for (int z = -r; z <= r; z++) {
            if (!mc.world.isChunkLoaded((center.getX() + x) >> 4, (center.getZ() + z) >> 4)) continue;
            for (int y = -h; y <= h; y++) {
                BlockPos p = center.add(x, y, z); var state = mc.world.getBlockState(p);
                if (!state.isAir() && state.isFullCube(mc.world, p)) {
                    Vec3d pos = Vec3d.ofCenter(p).subtract(origin);
                    blocks.add(new BlockSnapshot.Block(pos.x, pos.y, pos.z, state.getMapColor(mc.world, p).color & 0xffffff));
                }
            }
        }
        snapshotSequence = 0; snapshotQueue.addAll(BlockSnapshot.encode(blocks));
        return feedback("UEへ周辺 " + blocks.size() + " ブロックを送信します（手動プレビュー）");
    }
    private int clearPreview() {
        if (transport == null) return feedback("Bridgeが接続されていません");
        snapshotQueue.clear(); snapshotSequence = 0;
        sendEvent("block_preview_clear", origin, null); return feedback("UEプレビューの消去を送信しました");
    }
    private int worldEnabled(boolean enabled) {
        int result = change(v -> v.worldSync = enabled, false);
        if (config.worldSync == enabled) {
            worldSync.reset();
            if (transport != null && origin != null) sendEvent("world_clear", origin, null);
        }
        return result;
    }
    private int refreshWorld() {
        worldSync.reset();
        if (transport != null && origin != null) sendEvent("world_clear", origin, null);
        return feedback("周辺ワールドを再送信します。/uebridge world on で自動同期を有効にしてください");
    }
    private int exportTextures() {
        if(textureJob!=null && textureJob.running()) return feedback(textureJob.status());
        MinecraftClient mc=MinecraftClient.getInstance(); var resources=mc.getResourceManager();
        ArrayList<TextureExport.Block> blocks=new ArrayList<>();
        for(var block:Registries.BLOCK) {
            java.util.Map<String,String> properties=new java.util.LinkedHashMap<>();
            block.getDefaultState().getEntries().forEach((key,value)->properties.put(key.getName(),value.toString()));
            blocks.add(new TextureExport.Block(Registries.BLOCK.getId(block).toString(),properties));
        }
        blocks.sort(java.util.Comparator.comparing(TextureExport.Block::id));
        textureJob=new TextureExportJob(name->{
            var resource=resources.getResource(Identifier.of(name)); if(resource.isEmpty()) return null;
            try(var stream=resource.get().getInputStream()) { return stream.readNBytes(4*1024*1024+1); }
        },java.util.List.copyOf(blocks),FabricLoader.getInstance().getGameDir().resolve("uebridge-export"),
                message->mc.execute(()->feedback(message)));
        return feedback("使用中のブロックテクスチャを別スレッドで書き出します。/uebridge textures で進捗を確認できます");
    }
    private int change(Consumer<BridgeConfig> update, boolean reconnect) {
        if (configError != null) return feedback(configError + "。設定を修正して /uebridge reload を実行してください");
        BridgeConfig next = config.copy(); update.accept(next);
        try { next.save(configPath); config = next; if (reconnect) disconnect(); return feedback(status()); }
        catch (IOException | IllegalArgumentException e) { return feedback("設定を保存できません: " + e.getMessage()); }
    }
    private void commands() {
        ClientCommandRegistrationCallback.EVENT.register((dispatcher, registry) -> dispatcher.register(literal("uebridge")
            .executes(c -> feedback(status()))
            .then(literal("status").executes(c -> feedback(status())))
            .then(literal("on").executes(c -> change(v -> v.enabled = true, true)))
            .then(literal("off").executes(c -> change(v -> v.enabled = false, true)))
            .then(literal("recenter").executes(c -> { disconnect(); return feedback("次のフレームの足元を同期原点にします"); }))
            .then(literal("reload").executes(c -> { reload(); return feedback(configError == null ? status() : configError); }))
            .then(literal("port").then(argument("value", IntegerArgumentType.integer(1024, 65535))
                .executes(c -> change(v -> v.port = IntegerArgumentType.getInteger(c, "value"), true))))
            .then(literal("rate").then(argument("value", IntegerArgumentType.integer(20, 240))
                .executes(c -> change(v -> v.inputHz = IntegerArgumentType.getInteger(c, "value"), false))))
            .then(literal("bow").then(literal("on").executes(c -> change(v -> v.bowEvents = true, false)))
                .then(literal("off").executes(c -> change(v -> v.bowEvents = false, false))))
            .then(literal("world").executes(c -> feedback("自動同期="+config.worldSync+" / 領域="+worldSync.synchronizedCells()+"/"+worldSync.targetCells()))
                .then(literal("on").executes(c -> worldEnabled(true)))
                .then(literal("off").executes(c -> worldEnabled(false)))
                .then(literal("refresh").executes(c -> refreshWorld()))
                .then(literal("radius").then(argument("value", IntegerArgumentType.integer(1,3))
                    .executes(c -> change(v -> v.worldRadius = IntegerArgumentType.getInteger(c,"value"), false)))))
            .then(literal("video").executes(c -> feedback(video == null ? "UE映像OFF" : video.status()))
                .then(literal("on").executes(c -> change(v -> v.videoMode = 1, false)))
                .then(literal("pip").executes(c -> change(v -> v.videoMode = 1, false)))
                .then(literal("fullscreen").executes(c -> change(v -> v.videoMode = 2, false)))
                .then(literal("off").executes(c -> change(v -> v.videoMode = 0, false)))
                .then(literal("quality").then(literal("low").executes(c -> change(v -> v.videoQuality=0,false)))
                    .then(literal("balanced").executes(c -> change(v -> v.videoQuality=1,false)))
                    .then(literal("high").executes(c -> change(v -> v.videoQuality=2,false))))
                .then(literal("exposure").then(argument("value",DoubleArgumentType.doubleArg(-6,6))
                    .executes(c -> change(v -> v.videoExposure=DoubleArgumentType.getDouble(c,"value"),false))))
                .then(literal("port").then(argument("value", IntegerArgumentType.integer(1024,65535))
                    .executes(c -> change(v -> v.videoPort = IntegerArgumentType.getInteger(c,"value"), true)))))
            .then(literal("textures").executes(c -> feedback(textureJob==null ? "未書き出し。/uebridge textures export" : textureJob.status()))
                .then(literal("export").executes(c -> exportTextures())))
            .then(literal("preview").executes(c -> snapshot()).then(literal("clear").executes(c -> clearPreview())))));
    }
    private String status() {
        if (configError != null) return "UE Bridge: " + configError;
        if (!config.enabled) return "UE Bridge: OFF";
        if (transport == null) return "UE Bridge: ワールド待機 / port=" + config.port;
        var d = transport.diagnostics();
        return String.format(java.util.Locale.ROOT, "UE Bridge: %s / Camera=%s VFX=%s 壁=%d / RTT=%.1fms / 未ACK=%d 期限切れ=%d / %dHz",
                d.connected() ? ("diagnostic".equals(d.receiver()) ? "UDP診断ツール" : "UE応答あり") : "UE応答待ち（旧版UEはstatus非対応）", d.cameraReady(), d.vfxReady(), d.walls(),
                d.rttMillis(), d.pending(), d.expired(), config.inputHz)
                + " / World=" + worldSync.synchronizedCells()+"/"+worldSync.targetCells()
                + " / UE=" + d.build() + (config.worldSync && !d.worldSupported() ? "（ワールド同期対応UEを待っています）" : "")
                + " / Video=" + (video == null ? "OFF" : video.status())
                + " / 画質="+new String[]{"low","balanced","high"}[config.videoQuality]+" 露出="+config.videoExposure
                + " / テクスチャ="+d.textureMaterials()+"種類";
    }
    private int feedback(String text) {
        MinecraftClient mc = MinecraftClient.getInstance();
        if (mc.player != null) mc.player.sendMessage(Text.literal(text), false); return 1;
    }
    private void notifyPlayer(String text) {
        var player = MinecraftClient.getInstance().player; if (player != null) player.sendMessage(Text.literal(text), true);
    }
    private static int axis(boolean positive, boolean negative) { return (positive ? 1 : 0) - (negative ? 1 : 0); }
    private void position(JsonObject p, Vec3d pos) {
        p.addProperty("x", pos.x - origin.x); p.addProperty("y", pos.y - origin.y); p.addProperty("z", pos.z - origin.z);
    }
    private void report(IOException e) {
        long now = System.nanoTime();
        if (lastError == 0 || now - lastError > 5_000_000_000L) { LOG.warn("UE bridge: {}", e.toString()); lastError = now; }
    }
    private void disconnect() {
        if (video != null) video.close(); video = null; videoOverlay.setClient(null, 0); worldSync.reset();
        if (transport != null) try { transport.close(); } catch (IOException e) { report(e); }
        transport = null; origin = null; ignition.clear(); snapshotQueue.clear(); snapshotSequence = 0; lastConnected = false;
        lastReceiverId = ""; lastVideoConfig="";
        lastRetry = 0;
    }
}

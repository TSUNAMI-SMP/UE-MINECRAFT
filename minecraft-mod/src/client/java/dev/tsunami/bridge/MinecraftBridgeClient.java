package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import com.mojang.brigadier.arguments.IntegerArgumentType;
import com.mojang.brigadier.arguments.DoubleArgumentType;
import com.mojang.brigadier.arguments.StringArgumentType;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.command.v2.ClientCommandRegistrationCallback;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientEntityEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientLifecycleEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.event.player.UseBlockCallback;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.block.Blocks;
import net.minecraft.block.Block;
import net.minecraft.item.BlockItem;
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
    public static boolean skipWorldRender() { return instance!=null && instance.config.videoSkipVanilla && instance.config.videoMode==2
        && controllerMode() && instance.video!=null && instance.video.fresh() && !VanillaSkyComposite.active(); }
    public static VideoClient videoClient() { return instance==null ? null : instance.video; }
    public static boolean vanillaSkyRender() { return instance!=null && !instance.config.lighting && instance.config.vanillaSky
        && instance.config.videoMode==2 && controllerMode() && instance.video!=null && instance.video.fresh(); }
    public static void renderFrame() { if (instance != null) instance.frame(); }
    public static void dropSelectedItem(boolean entireStack) {
        if(instance!=null) ItemDropBridge.request(MinecraftClient.getInstance(),entireStack);
    }
    public static void inventoryDropBlocked() {
        if(instance==null) return;
        long now=System.nanoTime();
        if(instance.lastInventoryDropNotice==0 || now-instance.lastInventoryDropNotice>=2_000_000_000L) {
            instance.lastInventoryDropNotice=now;
            instance.notifyPlayer("UEで投下するにはインベントリを閉じ、主手に持って設定済み投下キーを押してください");
        }
    }
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
    private final ControllerActions actions = new ControllerActions();
    private final ControllerSprint sprint = new ControllerSprint();
    private final VanillaFeedback vanillaFeedback = new VanillaFeedback();
    private final MobBridge mobBridge = new MobBridge();
    private final MobFeedback mobFeedback = new MobFeedback();
    private final ArrayDeque<JsonObject> snapshotQueue = new ArrayDeque<>();
    private final Path configPath = FabricLoader.getInstance().getConfigDir().resolve("minecraft-ue-bridge.json");
    private BridgeConfig config = new BridgeConfig();
    private String configError;
    private BridgeTransport transport;
    private ClientWorld world;
    private Vec3d origin;
    private long lastFrame, lastError, lastRetry, reportedExpired, snapshotSequence;
    private long lastInventoryDropNotice;
    private boolean lastConnected;
    private String lastReceiverId = "";
    private String lastVideoConfig = "";
    private TextureExportJob textureJob;
    private NativePlayExport nativeJob;
    private final InitialImport initialImport=new InitialImport();
    private net.minecraft.item.ItemStack cachedModelStack=net.minecraft.item.ItemStack.EMPTY;
    private String cachedModelKey="";
    private final ControllerFlight flight=new ControllerFlight();
    private boolean controllerFrozen, controllerRequested;
    public static boolean controllerMode() {
        return instance!=null && instance.controllerFrozen && MinecraftClient.getInstance().player!=null;
    }

    @Override public void onInitializeClient() {
        instance = this;
        ItemDropBridge.configure(()->transport,()->controllerRequested,this::notifyPlayer);
        reload(); commands(); videoOverlay.register();
        UseBlockCallback.EVENT.register((player, w, hand, hit) -> {
            MinecraftClient mc = MinecraftClient.getInstance();
            if (!controllerMode() && w.isClient() && w == world && player == mc.player && transport != null && origin != null
                    && !player.isSpectator() && !player.isSneaking()
                    && w.getBlockState(hit.getBlockPos()).isOf(Blocks.TNT)
                    && (player.getStackInHand(hand).isOf(Items.FLINT_AND_STEEL)
                        || player.getStackInHand(hand).isOf(Items.FIRE_CHARGE))) {
                BlockPos p = hit.getBlockPos(); ignition.click(p.getX(), p.getY(), p.getZ(), System.nanoTime());
            }
            return ActionResult.PASS; // Never intercept vanilla interaction or mutate world/server state.
        });
        ClientEntityEvents.ENTITY_LOAD.register((entity, w) -> {
            if (!controllerMode() && w == world && entity instanceof TntEntity && transport != null && origin != null) {
                var p = ignition.primed(entity.getX(), entity.getY(), entity.getZ(), System.nanoTime());
                if (p != null) sendEvent("tnt_ignite", new Vec3d(p.x() + 0.5, p.y() + 0.5, p.z() + 0.5), null);
            }
        });
        DiagnosticCapture.initialize();
        ClientTickEvents.END_CLIENT_TICK.register(mc -> { tick(mc); DiagnosticCapture.clientTick(mc); });
        ClientLifecycleEvents.CLIENT_STOPPING.register(mc -> { disconnect(); if(textureJob!=null) textureJob.close(); if(nativeJob!=null) nativeJob.close(); });
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
            playFeedback(mc);
            JsonObject p = transport.packet("input");
            position(p, new Vec3d(mc.player.getX(), mc.player.getY(), mc.player.getZ()));
            p.addProperty("yaw", mc.player.getYaw()); p.addProperty("pitch", mc.player.getPitch());
            boolean active = mc.currentScreen == null && !mc.isPaused();
            p.addProperty("forward", active ? axis(mc.options.forwardKey.isPressed(), mc.options.backKey.isPressed()) : 0);
            p.addProperty("right", active ? axis(mc.options.rightKey.isPressed(), mc.options.leftKey.isPressed()) : 0);
            p.addProperty("jump", active && mc.options.jumpKey.isPressed());
            boolean creative=mc.player.isCreative();
            var uePose=transport.authorityPose();
            p.addProperty("creative",creative);
            p.addProperty("flying",flight.sample(active && controllerInputReady(),creative,mc.options.jumpKey.isPressed(),
                uePose!=null && uePose.grounded(),now));
            p.addProperty("controller",controllerRequested && initialImport.phase()==InitialImport.Phase.READY && !mc.isPaused());
            p.addProperty("itemSession",controllerRequested);
            p.addProperty("itemEpoch",ItemDropBridge.epoch());
            p.addProperty("sneak", active && (controllerMode() ? mc.options.sneakKey.isPressed() : mc.player.isSneaking()));
            p.addProperty("eyeHeight", mc.player.getEyeHeight(mc.player.getPose()));
            p.addProperty("bodyHeight", mc.player.getHeight());
            boolean ueSprint = sprint.sample(active && controllerInputReady(),mc.options.forwardKey.isPressed(),
                mc.options.backKey.isPressed(),mc.options.sprintKey.isPressed(),mc.options.sneakKey.isPressed(),now);
            p.addProperty("sprint",controllerRequested ? ueSprint : active && !controllerFrozen && mc.player.isSprinting());
            // Base Minecraft vertical FOV. UE applies its own smooth sprint multiplier once.
            if(transport.playerVisualsSupported()) p.addProperty("cameraFov",mc.options.getFov().getValue());
            selectedItem(mc,p);
            if(transport.playerVisualsSupported()) PlayerVisualState.sample(mc,controllerRequested).entrySet().forEach(e->p.add(e.getKey(),e.getValue()));
            if(transport.terrainSupported()) {
                double eye=mc.options.sneakKey.isPressed() ? 1.27 : 1.62;
                Vec3d lightPosition=uePose==null ? mc.player.getEyePos() : origin.add(uePose.x(),uePose.y()+eye,uePose.z());
                VanillaLightingState.write(p,mc,lightPosition);
            }
            transport.input(p);
            ItemDropBridge.tick(mc);
            var clicks=actions.sample(active && controllerRequested && transport.actionsSupported()
                && transport.diagnostics().ueControl(),mc.options.attackKey.isPressed(),mc.options.useKey.isPressed(),now);
            if(clicks.breaking()) blockAction(mc,"break");
            else if(clicks.placing()) blockAction(mc,"place");
        } catch (IOException e) { report(e); }
    }
    private void selectedItem(MinecraftClient mc,JsonObject p) {
        var stack=mc.player.getMainHandStack();
        String item=stack.isEmpty() ? "" : Registries.ITEM.getId(stack.getItem()).toString();
        String block=""; int color=0xffffff;
        if(stack.getItem() instanceof BlockItem blockItem) {
            var state=blockItem.getBlock().getDefaultState();
            if(transport.blockModelsSupported() ? BlockGeometryCapture.supported(state) : Block.isShapeFullCube(state.getCollisionShape(mc.world,mc.player.getBlockPos()))) {
                block=Registries.BLOCK.getId(blockItem.getBlock()).toString();
                color=state.getMapColor(mc.world,mc.player.getBlockPos()).color & 0xffffff;
            }
        }
        p.addProperty("heldItem",item.length()<=128 ? item : "");
        if(!net.minecraft.item.ItemStack.areItemsAndComponentsEqual(cachedModelStack,stack)) {cachedModelStack=stack.copy();cachedModelKey=ItemModelExport.modelKey(mc,stack);}
        p.addProperty("heldModelKey",cachedModelKey);
        if(stack.getItem() instanceof net.minecraft.item.SpawnEggItem egg) p.addProperty("spawnType",Registries.ENTITY_TYPE.getId(egg.getEntityType(stack)).toString());
        p.addProperty("heldBlock",block.length()<=128 ? block : ""); p.addProperty("heldColor",color);
    }
    private void blockAction(MinecraftClient mc,String action) throws IOException {
        if(transport.availableEvents()==0) return;
        JsonObject p=transport.packet("event"); position(p,origin); p.addProperty("event","block_action");
        p.addProperty("action",action); p.addProperty("importId",initialImport.id());
        p.addProperty("yaw",mc.player.getYaw()); p.addProperty("pitch",mc.player.getPitch());
        p.addProperty("sneak",mc.options.sneakKey.isPressed());
        selectedItem(mc,p); transport.event(p);
        PlayerVisualState.swing(mc);
    }
    private void tick(MinecraftClient mc) {
        if(nativeJob!=null && nativeJob.running()) nativeJob.tick(mc);
        PlayerVisualState.tick(mc,controllerMode());
        ensureConnection(mc); if (transport == null) return;
        try {
            transport.pump();
            playFeedback(mc);
            var ready=transport.diagnostics();
            mobBridge.tick(mc,transport,origin,initialImport.id(),controllerInputReady(),config.worldRadius,config.worldHalfHeight);
            if (ready.connected() && (!lastConnected || !ready.receiverId().equals(lastReceiverId))) {
                lastReceiverId=ready.receiverId(); lastVideoConfig=""; if(!initialImport.ownsWorld()) worldSync.reset();
            }
            initialImport.observe(ready.receiverId(),ready.importId(),ready.worldSealed());
            if(controllerRequested && (mc.getServer()==null || mc.getServer().isRemote())) {
                ItemDropBridge.stop(mc);
                controllerRequested=false;controllerFrozen=false;mobBridge.release();worldSync.pauseSource();sprint.reset();
                config.videoMode=0;
                notifyPlayer("LAN公開を検出したためUE操作を停止しました。専用シングルプレイで使用してください");
            }
            if(initialImport.phase()==InitialImport.Phase.LOST && (controllerRequested || controllerFrozen)) {
                ItemDropBridge.stop(mc);controllerRequested=false;controllerFrozen=false;worldSync.reset();
            }
            if(mc.currentScreen!=null || mc.isPaused() || !controllerInputReady() || mc.options.sneakKey.isPressed()) sprint.reset();
            String request=initialImport.request(System.nanoTime());
            if(!request.isEmpty() && ready.authoritySupported() && transport.diagnostics().pending()==0) {
                JsonObject p=transport.packet("event"); p.addProperty("event",request); position(p,origin);
                p.addProperty("importId",initialImport.id());
                if(request.equals("world_begin")) {
                    p.addProperty("ox",origin.x); p.addProperty("oy",origin.y); p.addProperty("oz",origin.z);
                } else p.addProperty("cells",worldSync.targetCells());
                transport.event(p);
            }
            if(initialImport.phase()==InitialImport.Phase.COPYING) {
                worldSync.tick(mc,origin,config,transport);
                if(worldSync.complete()) initialImport.copied();
            } else if(initialImport.phase()==InitialImport.Phase.READY && transport.terrainSupported() && controllerRequested) {
                worldSync.finishInitial();
                var pose=transport.authorityPose();
                worldSync.setControllerCenter(controllerRequested && pose!=null ? origin.add(pose.x(),pose.y(),pose.z()) : null);
                worldSync.tick(mc,origin,config,transport);
            } else if(config.worldSync && !initialImport.ownsWorld() && !ready.worldSealed()) worldSync.tick(mc,origin,config,transport);
            if (config.videoMode != 0 && video == null && transport.diagnostics().cameraReady() && transport.diagnostics().videoSupported())
                video = new VideoClient(config.videoPort, transport.session(),transport.videoV3Supported(),config.videoTransport);
            if (config.videoMode == 0 && video != null) { video.close(); video = null; }
            videoOverlay.setClient(video, config.videoMode); videoOverlay.setTransport(transport);
            String videoSettings=config.videoQuality+":"+config.targetFps+":"+config.videoTransport+":"+config.videoExposure+":"+config.lighting+":"+config.vanillaSky
                +":"+config.particleScale+":"+config.particleDensity+":"+config.particleLifetime;
            if(config.videoMode!=0 && ready.videoControlsSupported() && !videoSettings.equals(lastVideoConfig) && transport.availableEvents()>0) {
                int cap=Math.min(config.targetFps,transport.videoV2Supported() ? 60 : 30);
                int[][] presets={{480,270,cap,75},{960,540,cap,85},{1280,720,cap,90},{1920,1080,cap,90}}; int[] preset=presets[config.videoQuality];
                JsonObject p=transport.packet("event"); p.addProperty("event","video_config"); position(p,origin);
                p.addProperty("width",preset[0]); p.addProperty("height",preset[1]); p.addProperty("fps",preset[2]);
                p.addProperty("quality",preset[3]); p.addProperty("exposure",config.videoExposure);
                if(transport.videoV3Supported()) {
                    p.addProperty("lighting",config.lighting); p.addProperty("vanillaSky",config.vanillaSky);
                    p.addProperty("particleScale",config.particleScale); p.addProperty("particleDensity",config.particleDensity);
                    p.addProperty("particleLifetime",config.particleLifetime);
                }
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
            ItemDropBridge.tick(mc);
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
    private void playFeedback(MinecraftClient mc) {
        JsonObject effect;
        while((effect=transport.pollFeedback())!=null) {
            if(controllerRequested && mc.world!=null && mc.player!=null) {
                if("mob_feedback".equals(effect.get("kind").getAsString())) mobFeedback.accept(mc,effect);
                else vanillaFeedback.accept(mc,effect);
            }
        }
    }
    private boolean controllerInputReady() {
        if(!controllerRequested || transport==null || initialImport.phase()!=InitialImport.Phase.READY) return false;
        var d=transport.diagnostics();
        return d.connected() && d.authoritySupported() && d.worldSealed() && d.ueControl()
            && initialImport.id().equals(d.importId());
    }
    private void bow(LivingEntity user, float pull) {
        if (controllerMode() || !config.bowEvents || user != MinecraftClient.getInstance().player || transport == null || origin == null) return;
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
    private int startImport() {
        MinecraftClient mc=MinecraftClient.getInstance();
        if(transport==null || !transport.diagnostics().authoritySupported()) return feedback("UE 0.5.0へ接続してから実行してください");
        if(!transport.blockModelsSupported()) return feedback("このMODの初期転送にはUE0.9.0が必要です。UE更新ZIPを統合コピーして再ビルドしてください");
        if(!transport.blockPaletteReady()) return feedback("ブロックモデル素材が未準備です。/uebridge textures export 後、UEで最新素材を取り込んでレベルを保存し、Playを開始してください");
        if(!mc.isInSingleplayer() || mc.getServer()==null || mc.getServer().isRemote() || mc.player==null || mc.player.isSpectator()) return feedback("LAN公開していない専用シングルプレイで実行してください（観戦モードは対象外）");
        if(!mc.player.isOnGround() || mc.player.isSneaking()) return feedback("飛行を止め、地面に立ってしゃがまずに実行してください");
        if(transport.diagnostics().pending()!=0) return feedback("未ACKイベントが完了してから再実行してください");
        ItemDropBridge.stop(mc);
        mobBridge.release();
        if(transport.terrainSupported()) {
            config.worldRadius=config.terrainDistanceChunks*2;config.worldHalfHeight=6;
            config.videoQuality=3;config.targetFps=Math.max(30,config.targetFps);
        } else {
            config.worldRadius=Math.min(3,config.worldRadius);config.worldHalfHeight=Math.min(2,config.worldHalfHeight);
        }
        initialImport.start(transport.diagnostics().receiverId()); worldSync.beginInitial();
        controllerFrozen=true; controllerRequested=false; sprint.reset(); snapshotQueue.clear(); ignition.clear();
        return feedback("UE初期地形転送を開始。Minecraftの移動を固定します。"
            +(transport.terrainSupported() ? "目標="+config.terrainDistanceChunks+"チャンク / 1080p / "+config.targetFps+"fps。" : "旧UE互換の小領域です。4〜6チャンクにはUE0.11.0も必要です。")
            +"/uebridge import で進捗、READY後 /uebridge control ue");
    }
    private int control(boolean enabled) {
        MinecraftClient mc=MinecraftClient.getInstance();
        if(enabled && (mc.getServer()==null || mc.getServer().isRemote())) return feedback("UE主体の操作はLAN公開していない専用シングルプレイで使用してください");
        if(enabled && (transport==null || initialImport.phase()!=InitialImport.Phase.READY || !transport.diagnostics().worldSealed()))
            return feedback("先に /uebridge import start を実行し、READYまで待ってください");
        if(!enabled) {ItemDropBridge.stop(mc);worldSync.pauseSource();}
        controllerRequested=enabled; controllerFrozen=enabled; sprint.reset();flight.reset();
        if(!enabled) mobBridge.release();
        if(!enabled) change(v->v.videoMode=0,false);
        else change(v->v.videoMode=2,false);
        return feedback(enabled ? "UE操作：設定済みの移動・ジャンプ・しゃがみキー。クリエイティブはジャンプ2度押しで飛行切替、ジャンプで上昇・しゃがみで下降。サバイバルは飛行不可。卵は取り込み済みの地上モブ素材を使用" : "UE入力を停止し、Minecraft操作へ戻しました。UE地形は保持します");
    }
    private int worldEnabled(boolean enabled) {
        if(initialImport.ownsWorld() || (transport!=null && transport.diagnostics().worldSealed())) return feedback("UE初期地形を保持中。置換する場合のみ /uebridge import start を使用してください");
        int result = change(v -> v.worldSync = enabled, false);
        if (config.worldSync == enabled) {
            worldSync.reset();
            if (transport != null && origin != null) sendEvent("world_clear", origin, null);
        }
        return result;
    }
    private int refreshWorld() {
        if(initialImport.ownsWorld() || (transport!=null && transport.diagnostics().worldSealed())) return feedback("初期転送後のUE地形を保持しています。再転送は /uebridge import start（UE変更を置換）");
        worldSync.reset();
        if (transport != null && origin != null) sendEvent("world_clear", origin, null);
        return feedback("周辺ワールドを再送信します。/uebridge world on で自動同期を有効にしてください");
    }
    private int exportTextures() {
        if(textureJob!=null && textureJob.running()) return feedback(textureJob.status());
        MinecraftClient mc=MinecraftClient.getInstance(); var resources=mc.getResourceManager();
        if(mc.world==null || mc.player==null) return feedback("当たり判定の書き出しにはワールドへ入ってください");
        ArrayList<TextureExport.Block> blocks=new ArrayList<>();
        for(var block:Registries.BLOCK) {
            blocks.add(BlockGeometryCapture.capture(block,mc));
        }
        blocks.sort(java.util.Comparator.comparing(TextureExport.Block::id));
        textureJob=new TextureExportJob(name->{
            var resource=resources.getResource(Identifier.of(name)); if(resource.isEmpty()) return null;
            try(var stream=resource.get().getInputStream()) { return stream.readNBytes(4*1024*1024+1); }
        },java.util.List.copyOf(blocks),FabricLoader.getInstance().getGameDir().resolve("uebridge-export"),
                message->mc.execute(()->feedback(message)));
        return feedback("使用中のブロックテクスチャを別スレッドで書き出します。/uebridge textures で進捗を確認できます");
    }
    private int exportNative(int chunks) {
        if(nativeJob!=null && nativeJob.running()) return feedback(nativeJob.status());
        if(controllerMode()) return feedback("UE同期操作を /uebridge control off で停止してから、書き出し元の開始地点で /uebridge native export を実行してください");
        if(textureJob!=null && textureJob.running()) return feedback("既存のテクスチャ書き出し完了後に実行してください");
        try {nativeJob=new NativePlayExport(MinecraftClient.getInstance(),FabricLoader.getInstance().getGameDir().resolve("uebridge-export"),chunks,this::feedback);
            return feedback("UE単独プレイ用にワールド・素材・HUD・キー設定・音声をまとめて書き出します。開始地点で待機してください。/uebridge native で進捗を確認できます");}
        catch(IOException | RuntimeException error) {return feedback("UEネイティブ書き出しを開始できません: "+error.getMessage());}
    }
    private int change(Consumer<BridgeConfig> update, boolean reconnect) {
        if (configError != null) return feedback(configError + "。設定を修正して /uebridge reload を実行してください");
        BridgeConfig next = config.copy(); update.accept(next);
        try {
            next.save(configPath);
            boolean videoChanged=next.videoPort!=config.videoPort || next.videoTransport!=config.videoTransport;
            config = next;
            if (reconnect) disconnect();
            else if(videoChanged && video!=null) {video.close();video=null;}
            return feedback(status());
        }
        catch (IOException | IllegalArgumentException e) { return feedback("設定を保存できません: " + e.getMessage()); }
    }
    private int exportItems() {
        MinecraftClient mc=MinecraftClient.getInstance();
        if(mc.world==null || mc.player==null) return feedback("ワールドへ入ってから /uebridge items export を実行してください");
        try {var path=ItemModelExport.export(mc,FabricLoader.getInstance().getGameDir().resolve("uebridge-export"));return feedback("アイテムモデル書き出し完了: "+path+"（未対応は manifest.json の excluded）");}
        catch(IOException | RuntimeException error) {return feedback("アイテム書き出し失敗: "+error.getMessage());}
    }
    private int exportPlayer() {
        MinecraftClient mc=MinecraftClient.getInstance();
        if(mc.player==null) return feedback("ワールドへ入ってから /uebridge player export を実行してください");
        PlayerSkinExport.export(mc,FabricLoader.getInstance().getGameDir().resolve("uebridge-export"),this::feedback);
        return 1;
    }
    private int lightingOff() {
        if(transport==null || !transport.skySupported()) return feedback("照明と空の切替にはUE/MOD両方の0.9.0が必要です。/uebridge status");
        return change(v -> {v.lighting=false;v.vanillaSky=true;v.videoMode=2;},false);
    }
    private int exportMobs() {
        MinecraftClient mc=MinecraftClient.getInstance();
        if(mc.world==null || mc.player==null) return feedback("モブのいる専用ワールドで /uebridge mobs export を実行してください");
        MobModelExport.export(mc,FabricLoader.getInstance().getGameDir().resolve("uebridge-export"),this::feedback);
        return 1;
    }
    private int respawn() {
        if(!controllerInputReady() || !transport.mobsSupported()) return feedback("UE操作中に使用してください");
        if(transport.playerHealth()>0) return feedback("UE体力が0になったときに使用してください");
        sendEvent("player_respawn",origin,p -> p.addProperty("importId",initialImport.id()));
        return feedback("UEの初期位置への復帰を要求しました");
    }
    private int performanceTarget(int chunks) {
        if(initialImport.ownsWorld() && initialImport.phase()!=InitialImport.Phase.READY)
            return feedback("初期取り込みがREADYになってから描画距離を変更してください");
        return change(v->{v.terrainDistanceChunks=chunks;v.videoQuality=3;v.targetFps=Math.max(30,v.targetFps);
            if(transport!=null && transport.terrainSupported()) {v.worldRadius=chunks*2;v.worldHalfHeight=6;}},false);
    }
    private int legacyWorldRadius(int radius) {
        if(transport!=null && transport.terrainSupported() && initialImport.ownsWorld())
            return feedback("UE地形の描画距離は /uebridge performance target 4（5または6）で変更してください");
        return change(v->v.worldRadius=radius,false);
    }
    private int summonMob(String value) {
        if(!controllerInputReady() || !transport.mobsSupported()) return feedback("READY後のUE操作中に使用してください");
        Identifier type=Identifier.tryParse(value);
        if(type==null || !Registries.ENTITY_TYPE.containsId(type)) return feedback("モブIDが見つかりません。例: /uebridge mobs summon minecraft:zombie");
        if(MobBridge.unsupportedMovement(type.toString())) return feedback("この版は地上モブが対象です。水中・飛行モブは未対応です");
        var pose=transport.authorityPose();if(pose==null) return feedback("UE位置の応答を待ってから実行してください");
        if(transport.availableEvents()==0) return feedback("イベント送信待ちです。少し待って再実行してください");
        MinecraftClient mc=MinecraftClient.getInstance();Vec3d look=mc.player.getRotationVec(1);
        Vec3d ahead=new Vec3d(look.x,0,look.z).normalize().multiply(2);
        sendEvent("mob_template_spawn",origin.add(pose.x(),pose.y(),pose.z()).add(ahead),p->{
            p.addProperty("importId",initialImport.id());p.addProperty("mobType",type.toString());
        });
        return feedback("UEに "+type+" の召喚を要求しました。失敗理由は /uebridge mobs と /uebridge status で確認できます");
    }
    private void commands() {
        ClientCommandRegistrationCallback.EVENT.register((dispatcher, registry) -> dispatcher.register(literal("uebridge")
            .executes(c -> feedback(status()))
            .then(literal("status").executes(c -> feedback(status())))
            .then(literal("diagnose").then(literal("export").executes(c -> {
                if(controllerMode()) return feedback("先に /uebridge control off を実行してください");
                if(nativeJob!=null && nativeJob.running()) return feedback("native export の完了を待ってください");
                if(config.enabled) return feedback("先に /uebridge off を実行して通常のMinecraftで記録してください");
                return DiagnosticCapture.start(MinecraftClient.getInstance(),this::feedback);
            })))
            .then(literal("native").executes(c->feedback(nativeJob==null ? "/uebridge native export でUE単独プレイ用パッケージを書き出します" : nativeJob.status()))
                .then(literal("export").executes(c->exportNative(4)).then(argument("chunks",IntegerArgumentType.integer(4,6)).executes(c->exportNative(IntegerArgumentType.getInteger(c,"chunks")))))
                .then(literal("cancel").executes(c->{if(nativeJob!=null) nativeJob.close();return feedback(nativeJob==null ? "書き出しは実行されていません" : nativeJob.status());})))
            .then(literal("performance").executes(c -> feedback((transport==null ? "接続待ち" : transport.perfStatus())+" / "+worldSync.sourceStatus()))
                .then(literal("target").then(argument("chunks",IntegerArgumentType.integer(4,6))
                    .executes(c -> performanceTarget(IntegerArgumentType.getInteger(c,"chunks"))))))
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
            .then(literal("import").executes(c -> feedback("初期地形="+initialImport.phase()+" / "+worldSync.synchronizedCells()+"/"+worldSync.targetCells()))
                .then(literal("start").executes(c -> startImport())))
            .then(literal("control").then(literal("ue").executes(c -> control(true)))
                .then(literal("off").executes(c -> control(false))))
            .then(literal("items").executes(c -> feedback(ItemDropBridge.status()))
                .then(literal("export").executes(c -> exportItems())))
            .then(literal("respawn").executes(c -> respawn()))
            .then(literal("lighting").executes(c -> feedback(transport==null ? "接続待ち" : transport.renderStatus()))
                .then(literal("on").executes(c -> change(v -> {v.lighting=true;v.vanillaSky=false;},false)))
                .then(literal("off").executes(c -> lightingOff())))
            .then(literal("sky").then(literal("vanilla").executes(c -> lightingOff()))
                .then(literal("ue").executes(c -> change(v -> v.vanillaSky=false,false))))
            .then(literal("particles")
                .then(literal("scale").then(argument("value",DoubleArgumentType.doubleArg(.25,2))
                    .executes(c -> change(v -> v.particleScale=DoubleArgumentType.getDouble(c,"value"),false))))
                .then(literal("density").then(argument("value",DoubleArgumentType.doubleArg(.125,1))
                    .executes(c -> change(v -> v.particleDensity=DoubleArgumentType.getDouble(c,"value"),false))))
                .then(literal("lifetime").then(argument("value",DoubleArgumentType.doubleArg(.25,2))
                    .executes(c -> change(v -> v.particleLifetime=DoubleArgumentType.getDouble(c,"value"),false)))))
            .then(literal("world").executes(c -> feedback("自動同期="+config.worldSync+" / 領域="+worldSync.synchronizedCells()+"/"+worldSync.targetCells()))
                .then(literal("on").executes(c -> worldEnabled(true)))
                .then(literal("off").executes(c -> worldEnabled(false)))
                .then(literal("refresh").executes(c -> refreshWorld()))
                .then(literal("radius").then(argument("value", IntegerArgumentType.integer(1,3))
                    .executes(c -> legacyWorldRadius(IntegerArgumentType.getInteger(c,"value"))))))
            .then(literal("video").executes(c -> feedback(video == null ? "UE映像OFF" : video.status()))
                .then(literal("on").executes(c -> change(v -> {v.videoMode=1;v.vanillaSky=false;}, false)))
                .then(literal("pip").executes(c -> change(v -> {v.videoMode=1;v.vanillaSky=false;}, false)))
                .then(literal("fullscreen").executes(c -> change(v -> v.videoMode = 2, false)))
                .then(literal("off").executes(c -> change(v -> v.videoMode = 0, false)))
                .then(literal("optimize").then(literal("on").executes(c -> change(v -> v.videoSkipVanilla=true,false)))
                    .then(literal("off").executes(c -> change(v -> v.videoSkipVanilla=false,false))))
                .then(literal("quality").then(literal("low").executes(c -> change(v -> v.videoQuality=0,false)))
                    .then(literal("balanced").executes(c -> change(v -> v.videoQuality=1,false)))
                    .then(literal("high").executes(c -> change(v -> v.videoQuality=2,false)))
                    .then(literal("ultra").executes(c -> change(v -> v.videoQuality=3,false))))
                .then(literal("fps").then(argument("value",IntegerArgumentType.integer(1,60))
                    .executes(c -> change(v -> v.targetFps=IntegerArgumentType.getInteger(c,"value"),false))))
                .then(literal("transport").then(literal("auto").executes(c -> change(v -> v.videoTransport=0,false)))
                    .then(literal("jpeg").executes(c -> change(v -> v.videoTransport=1,false)))
                    .then(literal("gpu").executes(c -> change(v -> v.videoTransport=2,false))))
                .then(literal("exposure").then(argument("value",DoubleArgumentType.doubleArg(-6,6))
                    .executes(c -> change(v -> v.videoExposure=DoubleArgumentType.getDouble(c,"value"),false))))
                .then(literal("port").then(argument("value", IntegerArgumentType.integer(1024,65535))
                    .executes(c -> change(v -> v.videoPort = IntegerArgumentType.getInteger(c,"value"), true)))))
            .then(literal("textures").executes(c -> feedback(textureJob==null ? "未書き出し。/uebridge textures export" : textureJob.status()))
                .then(literal("export").executes(c -> exportTextures())))
            .then(literal("player").executes(c->feedback("/uebridge player export で現在のスキンを書き出します。視点切り替えはMinecraftの設定済みキーを使います"))
                .then(literal("export").executes(c->exportPlayer())))
            .then(literal("mobs").executes(c -> feedback(mobBridge.status()))
                .then(literal("export").executes(c -> exportMobs()))
                .then(literal("summon").then(argument("type",StringArgumentType.word())
                    .executes(c -> summonMob(StringArgumentType.getString(c,"type"))))))
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
                + " / 目標="+config.terrainDistanceChunks+"チャンク "+config.targetFps+"fps / "+worldSync.sourceStatus()
                + " / "+transport.perfStatus()
                + " / UE=" + d.build() + (config.worldSync && !d.worldSupported() ? "（ワールド同期対応UEを待っています）" : "")
                + " / Video=" + (video == null ? "OFF" : video.status())
                + " / 画質="+new String[]{"low","balanced","high","ultra"}[config.videoQuality]+" 露出="+config.videoExposure
                + " / テクスチャ="+d.textureMaterials()+"種類"
                + (transport.blockModelError().isEmpty() ? "" : " モデルエラー="+transport.blockModelError())
                + " / "+transport.renderStatus()+" / "+transport.mobStatus()
                + " UE体力="+transport.playerHealth()
                + " / "+transport.particleDiagnostics().summary()
                + " / MC描画省略="+skipWorldRender()+" / 操作="+transport.lastAction()+" / "+videoOverlay.timing()
                + " / 視点="+MinecraftClient.getInstance().options.getPerspective()+" Avatar="+transport.playerVisualsSupported()
                + " / 初期地形="+initialImport.phase()+" UE判定="+d.ueControl()+" 保持="+d.worldSealed()
                + (transport.authorityPose()==null ? "" : " / UE位置="+transport.authorityPose());
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
        ItemDropBridge.stop(MinecraftClient.getInstance());
        mobBridge.release();
        actions.reset();
        sprint.reset();
        vanillaFeedback.reset();
        mobFeedback.reset();
        PlayerVisualState.reset();
        if (video != null) video.close(); video = null; videoOverlay.setClient(null, 0); worldSync.reset();
        if (transport != null) try { transport.close(); } catch (IOException e) { report(e); }
        initialImport.reset(); controllerFrozen=controllerRequested=false;flight.reset();
        transport = null; origin = null; ignition.clear(); snapshotQueue.clear(); snapshotSequence = 0; lastConnected = false;
        lastReceiverId = ""; lastVideoConfig="";
        lastRetry = 0;
    }
}

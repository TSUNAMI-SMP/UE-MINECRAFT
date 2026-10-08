package dev.tsunami.bridge;

import com.google.gson.*;
import dev.tsunami.bridge.mixin.DiagnosticMobAccessor;
import net.fabricmc.loader.api.FabricLoader;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import java.util.concurrent.atomic.AtomicBoolean;
import net.minecraft.client.MinecraftClient;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.entity.ai.goal.GoalSelector;
import net.minecraft.entity.attribute.EntityAttributes;
import net.minecraft.item.BlockItem;
import net.minecraft.registry.Registries;
import net.minecraft.registry.RegistryKey;
import net.minecraft.world.World;
import net.minecraft.util.Identifier;
import net.minecraft.util.TypeFilter;
import net.minecraft.util.hit.BlockHitResult;
import net.minecraft.util.math.*;
import net.minecraft.server.MinecraftServer;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.time.Instant;
import java.util.*;
import java.util.concurrent.*;
import java.util.function.Consumer;

/** Explicit, read-only capture. Game objects are sampled on their owning threads. */
final class DiagnosticCapture {
    private static final Gson JSON=new GsonBuilder().setPrettyPrinting().create();
    private static volatile Job active;
    private static final class Job {
        final MinecraftClient client;final MinecraftServer server;final RegistryKey<World> dimension;
        final Vec3d center;final UUID playerId;final Consumer<String> feedback;final Path path;
        final Map<String,byte[]> resources=new LinkedHashMap<>();final JsonObject snapshot=new JsonObject();
        final JsonArray frames=new JsonArray();final long started=System.nanoTime();final AtomicBoolean finishQueued=new AtomicBoolean();volatile boolean completed;boolean archiveCreated;int ticks;
        Job(MinecraftClient c,Consumer<String> f,Path p) {client=c;server=c.getServer();dimension=c.world.getRegistryKey();center=c.player.getEntityPos();playerId=c.player.getUuid();feedback=f;path=p;}
    }
    static void initialize() {
        ServerTickEvents.END_SERVER_TICK.register(DiagnosticCapture::serverTick);
        ServerLifecycleEvents.SERVER_STOPPING.register(server->{Job job=active;if(job!=null && job.server==server) finish(job,"server_stopping");});
    }
    static int start(MinecraftClient client,Consumer<String> feedback) {
        if(active!=null) {feedback.accept("診断の取得中です。完了を待ってください");return 0;}
        if(client.world==null || client.player==null || client.getServer()==null || client.getServer().isRemote()) {feedback.accept("診断はシングルプレイのワールドを開いて実行してください");return 0;}
        try {
            Path dir=FabricLoader.getInstance().getGameDir().resolve("uebridge-export");Files.createDirectories(dir);
            Job job=new Job(client,feedback,dir.resolve("diagnostics-"+Instant.now().toString().replace(':','-')+"-"+UUID.randomUUID().toString().substring(0,8)+".zip"));
            captureClient(job);active=job;
            feedback.accept("診断記録を開始しました（200サーバーティック、通常約10秒）。メニューを閉じ、近くのモブを攻撃してノックバックを記録してください");return 1;
        } catch(Exception e) {feedback.accept("診断開始失敗: "+e.getClass().getSimpleName()+": "+e.getMessage());return 0;}
    }
    static void clientTick(MinecraftClient client) {
        Job job=active;if(job==null || job.completed) return;
        if(client.getServer()!=job.server || client.world==null || System.nanoTime()-job.started>TimeUnit.SECONDS.toNanos(90)) {if(job.finishQueued.compareAndSet(false,true)) job.server.execute(()->finish(job,"interrupted_or_timeout"));}
    }
    private static void captureClient(Job job) throws IOException {
        var c=job.client;JsonObject s=job.snapshot;
        s.addProperty("schema","uebridge.diagnostics.v1");s.addProperty("modVersion","0.15.3");s.addProperty("minecraftVersion","1.21.11");
        s.addProperty("mappingVersion","1.21.11+build.6");s.addProperty("createdUtc",Instant.now().toString());s.addProperty("dimension",job.dimension.getValue().toString());
        // Explicit whitelist: no key bindings, inventory names, servers, chat or account details.
        JsonObject settings=new JsonObject();
        settings.addProperty("mouseSensitivity",c.options.getMouseSensitivity().getValue());settings.addProperty("invertYMouse",c.options.getInvertMouseY().getValue());settings.addProperty("invertXMouse",c.options.getInvertMouseX().getValue());settings.addProperty("rawMouseInput",c.options.getRawMouseInput().getValue());
        settings.addProperty("fov",c.options.getFov().getValue());settings.addProperty("fovEffectScale",c.options.getFovEffectScale().getValue());settings.addProperty("smoothCamera",c.options.smoothCameraEnabled);settings.addProperty("guiScale",c.options.getGuiScale().getValue());settings.addProperty("bobView",c.options.getBobView().getValue());settings.addProperty("gamma",c.options.getGamma().getValue());
        settings.addProperty("perspective",c.options.getPerspective().name());settings.addProperty("yaw",c.player.getYaw());settings.addProperty("pitch",c.player.getPitch());settings.addProperty("language",c.getLanguageManager().getLanguage());settings.addProperty("forceUnicodeFont",c.options.getForceUnicodeFont().getValue());
        s.add("settings",settings);s.add("position",vector(job.center));s.addProperty("timeOfDay",c.world.getTimeOfDay());s.addProperty("rainGradient",c.world.getRainGradient(1));
        s.addProperty("heldItem",Registries.ITEM.getId(c.player.getMainHandStack().getItem()).toString());
        JsonArray blocks=new JsonArray();Set<net.minecraft.block.Block> types=new LinkedHashSet<>();types.add(net.minecraft.block.Blocks.GRASS_BLOCK);
        if(c.player.getMainHandStack().getItem() instanceof BlockItem b) types.add(b.getBlock());
        if(c.crosshairTarget instanceof BlockHitResult hit) {s.addProperty("targetBlock",hit.getBlockPos().toShortString());types.add(c.world.getBlockState(hit.getBlockPos()).getBlock());}
        BlockPos origin=c.player.getBlockPos();int unloaded=0;
        for(int x=-8;x<=8;x++) for(int z=-8;z<=8;z++) for(int y=-4;y<=4;y++) {
            BlockPos p=origin.add(x,y,z);if(!c.world.isChunkLoaded(p.getX()>>4,p.getZ()>>4)) {unloaded++;continue;}
            var state=c.world.getBlockState(p);if(state.isAir()) continue;
            JsonObject row=new JsonObject();row.addProperty("x",p.getX());row.addProperty("y",p.getY());row.addProperty("z",p.getZ());
            row.addProperty("block",Registries.BLOCK.getId(state.getBlock()).toString());row.addProperty("state",BlockGeometryCapture.stateKey(state));
            blocks.add(row);if(types.size()<8) types.add(state.getBlock());
        }
        s.add("nearbyBlocks",blocks);s.addProperty("unloadedCellsSkipped",unloaded);
        JsonArray geometry=new JsonArray();ArrayDeque<String> queue=new ArrayDeque<>();
        for(var block:types) {
            if(block.getStateManager().getStates().size()<=256) geometry.add(JSON.toJsonTree(BlockGeometryCapture.capture(block,c)));
            Identifier id=Registries.BLOCK.getId(block);queue.add(id.getNamespace()+":blockstates/"+id.getPath()+".json");
        }
        Identifier held=Registries.ITEM.getId(c.player.getMainHandStack().getItem());queue.add(held.getNamespace()+":items/"+held.getPath()+".json");
        s.add("blockGeometry",geometry);JsonArray missing=new JsonArray();Set<String> visited=new HashSet<>();long total=0;
        while(!queue.isEmpty() && visited.size()<256) {
            String id=queue.removeFirst();if(!visited.add(id)) continue;
            var found=c.getResourceManager().getResource(Identifier.of(TextureExport.id(id)));
            if(found.isEmpty()) {missing.add(id);continue;}
            byte[] data=DiagnosticArchive.read(found.get().getInputStream());total+=data.length;if(total>16L*1024*1024) throw new IOException("Selected resource budget exceeded");
            job.resources.put("resources/"+id.replace(':','/'),data);
            if(id.endsWith(".json")) references(JsonParser.parseString(new String(data,StandardCharsets.UTF_8)),queue);
        }
        s.add("missingResources",missing);s.addProperty("resourceSelectionTruncated",!queue.isEmpty());
        JsonArray packs=new JsonArray();c.getResourceManager().streamResourcePacks().forEach(p->packs.add(p.getId()));s.add("activeResourcePacks",packs);
    }
    private static void references(JsonElement element,ArrayDeque<String> queue) throws IOException {
        if(element.isJsonArray()) {for(var e:element.getAsJsonArray()) references(e,queue);}
        else if(element.isJsonObject()) for(var entry:element.getAsJsonObject().entrySet()) {
            String key=entry.getKey();JsonElement e=entry.getValue();
            if((key.equals("model") || key.equals("parent")) && e.isJsonPrimitive() && e.getAsJsonPrimitive().isString()) {
                String id=TextureExport.id(e.getAsString());int colon=id.indexOf(':');queue.add(id.substring(0,colon+1)+"models/"+id.substring(colon+1)+".json");
            } else if(key.equals("textures") && e.isJsonObject()) {
                for(var texture:e.getAsJsonObject().entrySet()) if(texture.getValue().isJsonPrimitive() && texture.getValue().getAsJsonPrimitive().isString()) {
                    String value=texture.getValue().getAsString();if(value.startsWith("#")) continue;
                    String id=TextureExport.id(value);int colon=id.indexOf(':');queue.add(id.substring(0,colon+1)+"textures/"+id.substring(colon+1)+".png");
                }
            } else references(e,queue);
        }
    }
    private static JsonArray vector(Vec3d v) {JsonArray a=new JsonArray();a.add(v.x);a.add(v.y);a.add(v.z);return a;}
    private static JsonArray goals(GoalSelector selector) {
        JsonArray a=new JsonArray();for(var goal:selector.getGoals()) {JsonObject g=new JsonObject();g.addProperty("class",goal.getGoal().getClass().getName());g.addProperty("priority",goal.getPriority());g.addProperty("running",goal.isRunning());a.add(g);}return a;
    }
    private static void serverTick(MinecraftServer server) {
        Job job=active;if(job==null || job.completed || server!=job.server) return;
        try {
            var world=server.getWorld(job.dimension);if(world==null) {finish(job,"dimension_unavailable");return;}
            List<MobEntity> mobs=new ArrayList<>();world.collectEntitiesByType(TypeFilter.instanceOf(MobEntity.class),new Box(job.center.add(-32,-16,-32),job.center.add(32,16,32)),m->true,mobs,32);
            JsonObject frame=new JsonObject();frame.addProperty("tick",job.ticks);frame.addProperty("elapsedNanos",System.nanoTime()-job.started);JsonArray states=new JsonArray();
            for(var m:mobs) {
                JsonObject row=new JsonObject();row.addProperty("id",m.getUuidAsString());row.addProperty("type",Registries.ENTITY_TYPE.getId(m.getType()).toString());
                row.add("position",vector(m.getEntityPos()));row.add("velocity",vector(m.getVelocity()));row.addProperty("grounded",m.isOnGround());row.addProperty("yaw",m.getYaw());row.addProperty("pitch",m.getPitch());row.addProperty("headYaw",m.headYaw);row.addProperty("bodyYaw",m.bodyYaw);row.addProperty("health",m.getHealth());row.addProperty("alive",m.isAlive());row.addProperty("hurtTime",m.hurtTime);row.addProperty("deathTime",m.deathTime);
                if(m.getTarget()!=null) row.addProperty("targetType",Registries.ENTITY_TYPE.getId(m.getTarget().getType()).toString());
                if(job.ticks==0 || job.ticks%20==0) {
                    row.addProperty("movementSpeed",m.getAttributeValue(EntityAttributes.MOVEMENT_SPEED));row.addProperty("knockbackResistance",m.getAttributeValue(EntityAttributes.KNOCKBACK_RESISTANCE));
                    var access=(DiagnosticMobAccessor)m;row.add("goals",goals(access.bridgeDiagnosticGoals()));row.add("targetGoals",goals(access.bridgeDiagnosticTargets()));
                    row.addProperty("navigation",m.getNavigation().getClass().getName());row.addProperty("navigationIdle",m.getNavigation().isIdle());row.addProperty("moveControl",m.getMoveControl().getClass().getName());row.addProperty("lookControl",m.getLookControl().getClass().getName());
                }
                states.add(row);
            }
            var player=world.getPlayerByUuid(job.playerId);
            if(player!=null) {JsonObject pose=new JsonObject();pose.add("position",vector(player.getEntityPos()));pose.add("velocity",vector(player.getVelocity()));pose.addProperty("grounded",player.isOnGround());pose.addProperty("sprinting",player.isSprinting());pose.addProperty("yaw",player.getYaw());pose.addProperty("pitch",player.getPitch());pose.addProperty("attackCooldown",player.getAttackCooldownProgress(0));frame.add("player",pose);}
            frame.add("mobs",states);job.frames.add(frame);if(++job.ticks>=200) finish(job,"complete");
        } catch(Exception e) {finish(job,"capture_error: "+e.getClass().getSimpleName()+": "+e.getMessage());}
    }
    private static void finish(Job job,String reason) {
        if(active!=job || job.completed) return;job.completed=true;
        job.snapshot.addProperty("traceCompletion",reason);job.snapshot.addProperty("sampledTicks",job.ticks);job.snapshot.add("mobTrace",job.frames);
        byte[] snapshot=JSON.toJson(job.snapshot).getBytes(StandardCharsets.UTF_8);
        CompletableFuture.runAsync(()->{
            try {write(job,snapshot);job.client.execute(()->{if(active==job) active=null;job.feedback.accept("診断ZIPを保存しました: "+job.path.toAbsolutePath()+"（このZIPを添付してください）");});}
            catch(Exception e) {try {if(job.archiveCreated) Files.deleteIfExists(job.path);}catch(IOException ignored){}job.client.execute(()->{if(active==job) active=null;job.feedback.accept("診断ZIP保存失敗: "+e.getClass().getSimpleName()+": "+e.getMessage());});}
        });
    }
    private static void write(Job job,byte[] snapshot) throws IOException {
        ClassLoader loader=MinecraftClient.class.getClassLoader();var resolver=FabricLoader.getInstance().getMappingResolver();JsonArray report=new JsonArray();int captured=0;
        DiagnosticArchive archive=new DiagnosticArchive(job.path);job.archiveCreated=true;
        try(DiagnosticArchive zip=archive) {
            zip.put("snapshot.json",snapshot);for(var entry:job.resources.entrySet()) zip.put(entry.getKey(),entry.getValue());
            byte[] index=DiagnosticArchive.read(DiagnosticCapture.class.getResourceAsStream("/diagnostics/classes.tsv"));zip.put("classes.tsv",index);
            zip.put("mappings.tiny",DiagnosticArchive.read(DiagnosticCapture.class.getResourceAsStream("/diagnostics/mappings.tiny")));
            for(String line:new String(index,StandardCharsets.UTF_8).split("\\R")) {
                if(line.isBlank() || line.startsWith("#")) continue;String[] parts=line.split("\t");
                String runtime=resolver.mapClassName("intermediary",parts[0].replace('/','.'));JsonObject row=new JsonObject();row.addProperty("named",parts[1]);row.addProperty("runtime",runtime);
                byte[] data=null;
                try {data=DiagnosticArchive.read(loader.getResourceAsStream(runtime.replace('.','/')+".class"));if(!DiagnosticArchive.classFile(data)) {data=null;throw new IOException("Invalid class file");}}
                catch(IOException e) {row.addProperty("error",e.getMessage());}
                if(data!=null) {zip.put("classes/"+runtime.replace('.','/')+".class",data);row.addProperty("sha256",DiagnosticArchive.hash(data));row.addProperty("bytes",data.length);captured++;}
                report.add(row);
            }
            JsonObject manifest=new JsonObject();manifest.addProperty("runtimeNamespace",resolver.getCurrentRuntimeNamespace());manifest.addProperty("capturedClasses",captured);manifest.add("classes",report);
            zip.put("class-report.json",JSON.toJson(manifest).getBytes(StandardCharsets.UTF_8));
            zip.put("README.txt",("UEBridge 0.15.3 diagnostic capture for Minecraft Java 1.21.11.\nClass resources from installed game, NOT Java source, NOT guaranteed post-Mixin in-memory bytecode.\nYarn 1.21.11+build.6 names and selected members are included in mappings.tiny.\nClass-report lists missing resources and SHA-256. resources/ contains selected active-pack models/textures.\nSnapshot includes position, nearby loaded blocks and up to 32 mobs sampled for 200 server ticks.\nMob AI goals sampled once per second; Brain-based AI needs bytecode analysis as well.\nNo world modifications, account tokens, chat or whole game JARs are collected.\nCapture in vanilla singleplayer with UE control and bridge disabled; attack a nearby mob during recording.\ntraceCompletion and sampledTicks show interrupted/partial captures.\n").getBytes(StandardCharsets.UTF_8));
            if(captured==0) throw new IOException("No Minecraft class resources captured");
        }
    }
}

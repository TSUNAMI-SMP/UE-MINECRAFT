package dev.tsunami.bridge;

import com.google.gson.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.function.Consumer;
import net.minecraft.block.*;
import dev.tsunami.bridge.mixin.*;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.color.world.BiomeColors;
import net.minecraft.client.world.ClientWorld;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.entity.EquipmentSlot;
import net.minecraft.entity.player.PlayerModelPart;
import net.minecraft.entity.player.PlayerSkinType;
import net.minecraft.item.*;
import net.minecraft.registry.Registries;
import net.minecraft.sound.SoundCategory;
import net.minecraft.sound.SoundEvents;
import net.minecraft.util.Arm;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.*;
import net.minecraft.world.LightType;
import net.minecraft.world.attribute.EnvironmentAttributes;

/** One-command, immutable native-play package. Reads only already-loaded source chunks.
 * Geometry capture yields on the client thread; resource encoding and sound/font work run on a daemon.
 * No source-world edits, chunk-generation tickets, networking, inventory changes or hidden latest-export lookup.
 */
public final class NativePlayExport implements AutoCloseable {
    private enum Stage {BLOCKS,TEXTURES,PLAYER,ITEMS,MOBS,UI,FONT,SOUNDS,WORLD,FINISH,DONE,FAILED}
    private final Path directory;private final String id=UUID.randomUUID().toString();private final ClientWorld sourceWorld;
    private final Vec3d origin;private final WorldSnapshot.Cell center;private final int radius,halfHeight=6;
    private final Consumer<String> notify;private final JsonObject manifest=new JsonObject(),assets=new JsonObject(),settings,blockSounds=new JsonObject(),worldExcluded=new JsonObject();
    private final ArrayList<Block> blockRegistry=new ArrayList<>();private final ArrayList<TextureExport.Block> blocks=new ArrayList<>();
    private final List<WorldSnapshot.Cell> cells=new ArrayList<>();private final NativeFontExport.Resources resources;
    private Stage stage=Stage.BLOCKS;private int blockIndex,cellIndex,voxelIndex,rows;private long worldBytes,lastNotice;
    private final List<WorldSnapshot.Shape> voxels=new ArrayList<>();private final int[] skyTop=new int[64];
    private final int[][] biomeTints=new int[512][3];
    private BufferedWriter worldOutput;private ItemModelExport.Session itemSession;private NativeUiExport ui;
    private volatile Path workerResult;private volatile Throwable workerError;private volatile String workerStatus="";private volatile boolean cancelled;
    private Thread worker;private String status="準備中";

    public NativePlayExport(MinecraftClient client,Path root,int chunkRadius,Consumer<String> notify) throws IOException {
        if(!client.isOnThread() || client.world==null || client.player==null) throw new IOException("ワールドへ入ってから書き出してください");
        if(client.getServer()==null || client.getServer().isRemote()) throw new IOException("専用のシングルプレイで書き出してください（LAN公開中は対象外）");
        if(chunkRadius<4 || chunkRadius>6) throw new IOException("書き出し範囲は4〜6チャンクです");
        sourceWorld=client.world;origin=client.player.getEntityPos();center=WorldSnapshot.Cell.at((int)Math.floor(origin.x),(int)Math.floor(origin.y),(int)Math.floor(origin.z));radius=chunkRadius*2;this.notify=notify;
        Set<Long> missing=new LinkedHashSet<>();
        for(int x=-radius;x<=radius;x++) for(int z=-radius;z<=radius;z++) {
            int cx=center.x()+x,cz=center.z()+z;if(!sourceWorld.isChunkLoaded(Math.floorDiv(cx,2),Math.floorDiv(cz,2))) missing.add(ChunkPos.toLong(Math.floorDiv(cx,2),Math.floorDiv(cz,2)));
            for(int y=-halfHeight;y<=halfHeight;y++) cells.add(new WorldSnapshot.Cell(cx,center.y()+y,cz));
        }
        if(!missing.isEmpty()) throw new IOException("範囲内に未読込チャンクが "+missing.size()+" 個あります。描画距離を "+(chunkRadius+2)+" 以上にし、読込完了後に再実行してください。未読込を空気として書き出しません");
        cells.sort(Comparator.comparingInt(c->Math.abs(c.x()-center.x())+Math.abs(c.y()-center.y())+Math.abs(c.z()-center.z())));
        Files.createDirectories(root);directory=root.resolve("native-"+java.time.LocalDateTime.now().toString().replace(':','-')+"-"+id.substring(0,8));Files.createDirectory(directory);
        var manager=client.getResourceManager();resources=new NativeFontExport.Resources() {
            public byte[] read(String name) throws IOException {
                var value=manager.getResource(Identifier.of(TextureExport.id(name)));if(value.isEmpty()) return null;
                int limit=name.endsWith(".zip") ? 64*1024*1024 : 4*1024*1024;
                try(var in=value.get().getInputStream()) {byte[] bytes=in.readNBytes(limit+1);if(bytes.length>limit) throw new IOException("Resource byte limit: "+name);return bytes;}
            }
            public List<byte[]> all(String name) throws IOException {
                List<byte[]> result=new ArrayList<>();for(var resource:manager.getAllResources(Identifier.of(TextureExport.id(name)))) {try(var in=resource.getInputStream()) {byte[] bytes=in.readNBytes(1024*1024+1);if(bytes.length>1024*1024) throw new IOException("Resource JSON byte limit: "+name);result.add(bytes);}}return result;
            }
        };
        settings=captureSettings(client);manifest.addProperty("kind","native-play");manifest.addProperty("schema","uebridge.native.v1");manifest.addProperty("version",1);manifest.addProperty("id",id);
        manifest.addProperty("generatedAt",java.time.Instant.now().toString());manifest.addProperty("minecraftVersion","1.21.11");manifest.addProperty("radiusChunks",chunkRadius);manifest.add("settings",settings);
        for(Block block:Registries.BLOCK) blockRegistry.add(block);blockRegistry.sort(Comparator.comparing(block->Registries.BLOCK.getId(block).toString()));
        if(blockRegistry.size()>4096) throw new IOException("ブロック登録上限4096を超えています");
        manifest.addProperty("sourcePolicy","Read-only finite snapshot of already loaded chunks; no source edits or generation. Fluid geometry is included. Dedicated renderers use captured default item geometry; exclusions are listed.");
    }
    public boolean running() {return stage!=Stage.DONE && stage!=Stage.FAILED;}
    public String status() {return worker!=null && worker.isAlive() && !workerStatus.isEmpty()?workerStatus:status;}
    public Path directory() {return directory;}
    public void tick(MinecraftClient client) {
        if(!running()) return;
        if(cancelled || client.world!=sourceWorld || client.player==null) {fail(new IOException("ワールド退出・切り替えにより書き出しを中止しました"));return;}
        try {
            long deadline=System.nanoTime()+4_000_000L;
            switch(stage) {
                case BLOCKS -> {
                    do {if(blockIndex==blockRegistry.size()) break;Block block=blockRegistry.get(blockIndex++);blocks.add(BlockGeometryCapture.capture(block,client));captureBlockSounds(block);}
                    while(System.nanoTime()<deadline);
                    status="UE用ブロック情報 "+blockIndex+"/"+blockRegistry.size();
                    if(blockIndex==blockRegistry.size()) {stage=Stage.TEXTURES;startWorker(()->{
                        TextureExport export=new TextureExport(resources,directory.resolve("textures"));int count=0;
                        for(var block:blocks) {if(cancelled) throw new IOException("Cancelled");export.export(block);workerStatus="ブロック素材 "+(++count)+"/"+blocks.size();}
                        return export.finish();});}
                }
                case TEXTURES -> {if(takeWorker("textures")) {stage=Stage.PLAYER;status="プレイヤースキン";}}
                case PLAYER -> {addAsset("player",PlayerSkinExport.export(client,directory));itemSession=new ItemModelExport.Session(client,directory);stage=Stage.ITEMS;}
                case ITEMS -> {itemSession.advance(client,deadline);status="アイテムモデル "+itemSession.completed()+"/"+itemSession.total();if(itemSession.complete()) {addAsset("items",itemSession.finish(client));stage=Stage.MOBS;status="モブモデル（ゾンビ・村人を含む）";}}
                case MOBS -> {Path path=MobModelExport.export(client,directory);JsonObject mobManifest=JsonParser.parseString(Files.readString(path)).getAsJsonObject();var templates=mobManifest.getAsJsonObject("templates");
                    if(templates==null || !templates.has("minecraft:zombie") || !templates.has("minecraft:villager")) throw new IOException("ゾンビ・村人の標準モデルを取得できません。モブ書き出しの skipped を確認してください");
                    addAsset("mobs",path);ui=new NativeUiExport(client,directory,new int[]{(center.x()-radius)*8,(center.y()-halfHeight)*8,(center.z()-radius)*8,(center.x()+radius+1)*8-1,(center.y()+halfHeight+1)*8-1,(center.z()+radius+1)*8-1});stage=Stage.UI;}
                case UI -> {ui.advance(client,deadline);status="HUD・アイコン "+ui.completed()+"/"+ui.total();if(ui.complete()) {stage=Stage.FONT;startWorker(()->ui.finish(client,resources));}}
                case FONT -> {if(takeWorker("ui")) {stage=Stage.SOUNDS;Set<String> requested=new TreeSet<>();for(var e:blockSounds.entrySet()) for(String key:List.of("break","place","step","hit","fall","open","close","activate","deactivate")) {var entry=e.getValue().getAsJsonObject();if(entry.has(key)) requested.add(entry.get(key).getAsString());}
                    requested.addAll(List.of("minecraft:ui.button.click","minecraft:entity.player.hurt","minecraft:entity.player.big_fall","minecraft:entity.player.small_fall","minecraft:entity.item.pickup","minecraft:entity.generic.explode","minecraft:entity.tnt.primed","minecraft:block.lever.click","minecraft:entity.arrow.shoot","minecraft:entity.player.attack.strong","minecraft:entity.player.attack.weak","minecraft:entity.player.attack.nodamage","minecraft:entity.player.attack.knockback","minecraft:entity.player.attack.crit","minecraft:entity.player.attack.sweep"));
                    var definitions=soundDefinitions(client);for(String sound:definitions.keySet()) if(sound.startsWith("minecraft:entity.") && (sound.endsWith(".ambient") || sound.endsWith(".hurt") || sound.endsWith(".death") || sound.endsWith(".step"))) requested.add(sound);
                    startWorker(()->NativeSoundExport.export(resources,directory,definitions,blockSounds,requested,value->workerStatus=value));}}
                case SOUNDS -> {if(takeWorker("sounds")) {openWorld(client);stage=Stage.WORLD;}}
                case WORLD -> captureWorld(client,deadline);
                case FINISH -> finish();
                default -> { }
            }
            long now=System.nanoTime();if(now-lastNotice>=2_000_000_000L && running()) {lastNotice=now;notify.accept("UEネイティブ書き出し: "+status());}
        } catch(IOException | RuntimeException error) {fail(error);}
    }
    private void addAsset(String name,Path path) throws IOException {assets.add(name,NativeExportData.reference(directory,path));}
    private interface Work {Path run() throws IOException;}
    private void startWorker(Work work) {workerResult=null;workerError=null;workerStatus="";worker=new Thread(()->{try {workerResult=work.run();}catch(Throwable error) {workerError=error;}},"UE-Bridge-Native-Export");worker.setDaemon(true);worker.start();}
    private boolean takeWorker(String asset) throws IOException {
        if(worker!=null && worker.isAlive()) return false;
        if(workerError!=null) throw new IOException("書き出し工程 "+asset+": "+workerError.getMessage(),workerError);
        if(workerResult==null) throw new IOException("書き出し工程が完了しませんでした: "+asset);addAsset(asset,workerResult);worker=null;workerResult=null;return true;
    }
    private void captureBlockSounds(Block block) {
        var group=block.getDefaultState().getSoundGroup();JsonObject entry=new JsonObject();entry.addProperty("break",group.getBreakSound().id().toString());entry.addProperty("place",group.getPlaceSound().id().toString());
        entry.addProperty("step",group.getStepSound().id().toString());entry.addProperty("hit",group.getHitSound().id().toString());entry.addProperty("fall",group.getFallSound().id().toString());entry.addProperty("volume",group.getVolume());entry.addProperty("pitch",group.getPitch());
        if(block instanceof DoorBlock door) {
            entry.addProperty("open",door.getBlockSetType().doorOpen().id().toString());entry.addProperty("close",door.getBlockSetType().doorClose().id().toString());
        } else if(block instanceof TrapdoorBlock) {
            var set=((TrapdoorSoundAccessor)block).bridgeSoundType();entry.addProperty("open",set.trapdoorOpen().id().toString());entry.addProperty("close",set.trapdoorClose().id().toString());
        } else if(block instanceof FenceGateBlock) {
            var wood=((GateSoundAccessor)block).bridgeSoundType();entry.addProperty("open",wood.fenceGateOpen().id().toString());entry.addProperty("close",wood.fenceGateClose().id().toString());
        } else if(block instanceof ButtonBlock) {
            var set=((ButtonSoundAccessor)block).bridgeSoundType();entry.addProperty("activate",set.buttonClickOn().id().toString());entry.addProperty("deactivate",set.buttonClickOff().id().toString());
        } else if(block instanceof LeverBlock) {
            entry.addProperty("activate",SoundEvents.BLOCK_LEVER_CLICK.id().toString());entry.addProperty("deactivate",SoundEvents.BLOCK_LEVER_CLICK.id().toString());
        }
        if(entry.has("activate")) {entry.addProperty("interactionVolume",.3f);entry.addProperty("activatePitch",.6f);entry.addProperty("deactivatePitch",.5f);}
        else if(entry.has("open")) {entry.addProperty("interactionVolume",1f);entry.addProperty("interactionPitchMin",.9f);entry.addProperty("interactionPitchMax",1f);}
        blockSounds.add(Registries.BLOCK.getId(block).toString(),entry);
    }
    private Map<String,JsonObject> soundDefinitions(MinecraftClient client) throws IOException {
        Map<String,JsonObject> result=new LinkedHashMap<>();
        for(String namespace:client.getResourceManager().getAllNamespaces()) for(byte[] bytes:resources.all(namespace+":sounds.json")) {
            JsonObject json=JsonParser.parseString(new String(bytes,StandardCharsets.UTF_8)).getAsJsonObject();
            for(var e:json.entrySet()) {
                String id=namespace+":"+e.getKey();JsonObject value=e.getValue().getAsJsonObject().deepCopy();
                JsonObject old=result.get(id);if(old!=null && (!value.has("replace") || !value.get("replace").getAsBoolean()) && old.has("sounds")) {JsonArray merged=old.getAsJsonArray("sounds").deepCopy();if(value.has("sounds")) value.getAsJsonArray("sounds").forEach(merged::add);value.add("sounds",merged);}result.put(id,value);
            }
        }
        return result;
    }
    private void openWorld(MinecraftClient client) throws IOException {
        JsonObject header=new JsonObject();header.addProperty("type","native_world");header.addProperty("schema",1);header.addProperty("version",1);header.addProperty("id",id);
        header.addProperty("dimension",sourceWorld.getRegistryKey().getValue().toString());header.add("origin",NativeExportData.array(origin.x,origin.y,origin.z));header.add("spawn",NativeExportData.array(origin.x,origin.y,origin.z));
        header.addProperty("yaw",settings.get("yaw").getAsDouble());header.addProperty("pitch",settings.get("pitch").getAsDouble());header.add("center",NativeExportData.array(center.x(),center.y(),center.z()));header.addProperty("radius",radius);header.addProperty("halfHeight",halfHeight);header.addProperty("cells",cells.size());
        JsonObject packet=new JsonObject();VanillaLightingState.write(packet,client,origin.add(0,1.62,0));
        JsonObject light=packet.getAsJsonObject("vanillaLight");
        var attributes=sourceWorld.getEnvironmentAttributes();Vec3d skyPosition=origin.add(0,1.62,0);
        light.addProperty("skyBackgroundColor",attributes.getAttributeValue(EnvironmentAttributes.SKY_COLOR_VISUAL,skyPosition)&0xffffff);
        // Read actual dimension/positional attributes instead of assuming custom
        // worlds use the Overworld clock or the legacy moon atlas ordering.
        light.addProperty("skybox",sourceWorld.getDimension().skybox().asString());
        float sunAngle=attributes.getAttributeValue(EnvironmentAttributes.SUN_ANGLE_VISUAL,skyPosition);
        float moonAngle=attributes.getAttributeValue(EnvironmentAttributes.MOON_ANGLE_VISUAL,skyPosition);
        if(Float.isFinite(sunAngle)) light.addProperty("sunAngle",MathHelper.wrapDegrees(sunAngle));
        if(Float.isFinite(moonAngle)) light.addProperty("moonAngle",MathHelper.wrapDegrees(moonAngle));
        light.addProperty("moonPhase",attributes.getAttributeValue(EnvironmentAttributes.MOON_PHASE_VISUAL,skyPosition).getIndex());
        float starAngle=attributes.getAttributeValue(EnvironmentAttributes.STAR_ANGLE_VISUAL,skyPosition);
        float starBrightness=attributes.getAttributeValue(EnvironmentAttributes.STAR_BRIGHTNESS_VISUAL,skyPosition);
        if(Float.isFinite(starAngle)) light.addProperty("starAngle",MathHelper.wrapDegrees(starAngle));
        if(Float.isFinite(starBrightness)) light.addProperty("starBrightness",MathHelper.clamp(starBrightness,0,1));
        // Keep alpha: SkyRendering.renderGlowingSky uses it both for blend
        // strength and the fan's geometry, unlike the opaque sky RGB attribute.
        light.addProperty("sunriseAndSunsetColor",Integer.toUnsignedLong(attributes.getAttributeValue(EnvironmentAttributes.SUNRISE_SUNSET_COLOR_VISUAL,skyPosition)));
        light.addProperty("timeOfDay",sourceWorld.getTimeOfDay());light.addProperty("worldTime",sourceWorld.getTime());light.addProperty("rainGradient",sourceWorld.getRainGradient(1));light.addProperty("thunderGradient",sourceWorld.getThunderGradient(1));header.add("vanillaLight",light);
        header.add("mobs",captureMobs(client));worldOutput=Files.newBufferedWriter(directory.resolve("world.ndjson"),StandardCharsets.UTF_8,StandardOpenOption.CREATE_NEW);writeLine(header);
    }
    private JsonArray captureMobs(MinecraftClient client) {
        JsonArray result=new JsonArray();for(var entity:sourceWorld.getEntities()) {
            if(!(entity instanceof MobEntity mob) || !mob.isAlive() || result.size()>=128) continue;String appearance=MobModelExport.appearance(mob.getUuid());if(appearance==null || appearance.isEmpty()) continue;
            WorldSnapshot.Cell cell=WorldSnapshot.Cell.at(mob.getBlockX(),mob.getBlockY(),mob.getBlockZ());if(!cell.inside(center,radius,halfHeight)) continue;
            JsonObject value=new JsonObject();value.addProperty("id",mob.getUuidAsString());value.addProperty("type",Registries.ENTITY_TYPE.getId(mob.getType()).toString());value.addProperty("appearance",appearance);value.add("position",NativeExportData.array(mob.getX(),mob.getY(),mob.getZ()));value.addProperty("yaw",MathHelper.wrapDegrees(mob.getYaw()));value.addProperty("pitch",mob.getPitch());value.addProperty("health",mob.getHealth());result.add(value);
        }return result;
    }
    private final JsonArray waterVoxels=new JsonArray();
    private void captureWorld(MinecraftClient client,long deadline) throws IOException {
        int reads=0;do {
            if(cellIndex==cells.size()) {worldOutput.close();worldOutput=null;stage=Stage.FINISH;return;}
            var cell=cells.get(cellIndex);if(!sourceWorld.isChunkLoaded(Math.floorDiv(cell.x(),2),Math.floorDiv(cell.z(),2))) throw new IOException("書き出し中にチャンクが未読込になりました。開始地点で待って再実行してください");
            int cursor=voxelIndex++,x=cell.x()*8+(cursor&7),z=cell.z()*8+((cursor>>3)&7),y=cell.y()*8+(cursor<512?cursor>>6:8);BlockPos pos=new BlockPos(x,y,z);++reads;
            if(cursor<512) {
                biomeTints[cursor][0]=BiomeColors.getGrassColor(sourceWorld,pos)&0xffffff;
                biomeTints[cursor][1]=BiomeColors.getFoliageColor(sourceWorld,pos)&0xffffff;
                biomeTints[cursor][2]=BiomeColors.getDryFoliageColor(sourceWorld,pos)&0xffffff;
            }
            if(cursor>=512) skyTop[cursor-512]=Math.max(0,Math.min(15,sourceWorld.getLightLevel(LightType.SKY,pos)));
            else if(!sourceWorld.isOutOfHeightLimit(y)) {
                var state=sourceWorld.getBlockState(pos);
                if(state.getFluidState().isIn(net.minecraft.registry.tag.FluidTags.WATER)) waterVoxels.add(cursor);
                if(!state.isAir()) {
                    if(!BlockGeometryCapture.supported(state)) {String key=Registries.BLOCK.getId(state.getBlock())+" / "+BlockGeometryCapture.exclusion(state);worldExcluded.addProperty(key,worldExcluded.has(key)?worldExcluded.get(key).getAsInt()+1:1);}
                    else {int color=state.getMapColor(sourceWorld,pos).color&0xffffff,tint=client.getBlockColors().getColor(state,sourceWorld,pos,0);if(state.isLiquid() && state.getFluidState().isIn(net.minecraft.registry.tag.FluidTags.WATER)) color=BiomeColors.getWaterColor(sourceWorld,pos)&0xffffff;if(tint!=-1) color=tint&0xffffff;var offset=state.getModelOffset(pos);
                        voxels.add(new WorldSnapshot.Shape(x+.5+offset.x-origin.x,y+.5+offset.y-origin.y,z+.5+offset.z-origin.z,color,1,1,1,Registries.BLOCK.getId(state.getBlock()).toString(),false,x,y,z,BlockGeometryCapture.stateKey(state),1,
                            Math.max(0,Math.min(15,sourceWorld.getLightLevel(LightType.SKY,pos))),Math.max(0,Math.min(15,sourceWorld.getLightLevel(LightType.BLOCK,pos))),state.getOpacity(),state.getLuminance()));}
                }
            }
            if(voxelIndex==576) {rows+=voxels.size();if(rows>NativeExportData.MAX_ROWS) throw new IOException("UE安全上限2097152ブロックを超えました。4チャンクまたは空洞の多い範囲で書き出してください");JsonObject cellData=NativeExportData.cell(cell,voxels,skyTop);cellData.add("water",waterVoxels.deepCopy());cellData.add("biomeTints",BiomeTintSnapshot.encode(biomeTints));writeLine(cellData);waterVoxels.asList().clear();voxels.clear();voxelIndex=0;++cellIndex;}
        } while(reads<4096 && System.nanoTime()<deadline);
        status="地形 "+cellIndex+"/"+cells.size()+" セル / "+rows+" ブロック（開始地点でお待ちください）";
    }
    private void writeLine(JsonObject object) throws IOException {String json=object.toString();worldBytes+=json.getBytes(StandardCharsets.UTF_8).length+1;if(worldBytes>NativeExportData.MAX_WORLD_BYTES) throw new IOException("地形ファイル512 MiB上限を超えました");worldOutput.write(json);worldOutput.newLine();}
    private void finish() throws IOException {
        manifest.add("assets",assets);manifest.add("blockSounds",blockSounds);JsonObject world=NativeExportData.reference(directory,directory.resolve("world.ndjson"));world.addProperty("cells",cells.size());world.addProperty("blocks",rows);world.add("excluded",worldExcluded);world.addProperty("complete",true);manifest.add("world",world);
        Path file=NativeExportData.writeManifest(directory,manifest);stage=Stage.DONE;status="UEネイティブ書き出し完了: "+file.toString().replace('\\','/');notify.accept(status+" / Minecraftを終了できます。Play-Native.cmdでこのファイルを選択してください");
    }
    private void fail(Throwable error) {stage=Stage.FAILED;cancelled=true;if(worker!=null) worker.interrupt();if(worldOutput!=null) try {worldOutput.close();}catch(IOException ignored) {}worldOutput=null;status="UEネイティブ書き出し失敗: "+error.getMessage()+" / 未完了フォルダ: "+directory;
        try {Files.writeString(directory.resolve("FAILED.txt"),status,StandardOpenOption.CREATE,StandardOpenOption.TRUNCATE_EXISTING);}catch(IOException ignored) {}notify.accept(status);}
    @Override public void close() {if(running()) fail(new IOException("書き出しを中止しました"));}

    static JsonObject captureSettings(MinecraftClient client) {
        JsonObject settings=new JsonObject(),keys=new JsonObject();for(var key:client.options.allKeys) keys.addProperty(key.getId(),key.getBoundKeyTranslationKey());settings.add("keyBindings",keys);
        settings.addProperty("mouseSensitivity",client.options.getMouseSensitivity().getValue());settings.addProperty("invertYMouse",client.options.getInvertMouseY().getValue());settings.addProperty("invertXMouse",client.options.getInvertMouseX().getValue());
        settings.addProperty("rawMouseInput",client.options.getRawMouseInput().getValue());settings.addProperty("mouseWheelSensitivity",client.options.getMouseWheelSensitivity().getValue());settings.addProperty("fov",client.options.getFov().getValue());settings.addProperty("fovEffectScale",client.options.getFovEffectScale().getValue());settings.addProperty("guiScale",client.options.getGuiScale().getValue());settings.addProperty("bobView",client.options.getBobView().getValue());settings.addProperty("gamma",client.options.getGamma().getValue());
        settings.addProperty("smoothCamera",client.options.smoothCameraEnabled);
        settings.addProperty("attackIndicator",client.options.getAttackIndicator().getValue().name().toLowerCase(java.util.Locale.ROOT));
        settings.addProperty("mainHand",client.options.getMainArm().getValue()==Arm.LEFT?"left":"right");int layers=0;for(PlayerModelPart part:PlayerModelPart.values()) if(client.options.isPlayerModelPartEnabled(part)) layers|=part.getBitFlag();settings.addProperty("skinLayers",layers);settings.addProperty("slimArms",client.player.getSkin().model()==PlayerSkinType.SLIM);
        settings.addProperty("gameMode",client.player.isCreative()?"creative":"survival");settings.addProperty("perspective",client.options.getPerspective().isFirstPerson()?0:client.options.getPerspective().isFrontView()?2:1);settings.addProperty("yaw",MathHelper.wrapDegrees(client.player.getYaw()));settings.addProperty("pitch",client.player.getPitch());
        settings.addProperty("flying",client.player.getAbilities().flying);
        settings.addProperty("forceUnicodeFont",client.options.getForceUnicodeFont().getValue());
        settings.addProperty("sneakToggled",client.options.getSneakToggled().getValue());settings.addProperty("sprintToggled",client.options.getSprintToggled().getValue());settings.addProperty("attackToggled",client.options.getAttackToggled().getValue());settings.addProperty("useToggled",client.options.getUseToggled().getValue());settings.addProperty("autoJump",client.options.getAutoJump().getValue());
        settings.addProperty("language",client.getLanguageManager().getLanguage());settings.addProperty("selectedSlot",client.player.getInventory().getSelectedSlot());
        JsonArray inventory=new JsonArray(),hotbar=new JsonArray();for(int slot=0;slot<36;slot++) {JsonObject entry=stack(client,client.player.getInventory().getStack(slot));entry.addProperty("slot",slot);inventory.add(entry);if(slot<9) hotbar.add(entry.deepCopy());}settings.add("inventory",inventory);settings.add("hotbar",hotbar);settings.add("offhand",stack(client,client.player.getOffHandStack()));
        settings.addProperty("health",client.player.getHealth());settings.addProperty("food",client.player.getHungerManager().getFoodLevel());settings.addProperty("experienceLevel",client.player.experienceLevel);settings.addProperty("experienceProgress",client.player.experienceProgress);settings.addProperty("armor",client.player.getArmor());
        JsonArray equipment=new JsonArray();for(EquipmentSlot slot:List.of(EquipmentSlot.HEAD,EquipmentSlot.CHEST,EquipmentSlot.LEGS,EquipmentSlot.FEET)) equipment.add(stack(client,client.player.getEquippedStack(slot)));settings.add("equipment",equipment);
        JsonObject volumes=new JsonObject();for(SoundCategory category:SoundCategory.values()) volumes.addProperty(category.getName(),client.options.getSoundVolumeOption(category).getValue());settings.add("soundVolumes",volumes);settings.addProperty("soundMasterVolume",client.options.getSoundVolumeOption(SoundCategory.MASTER).getValue());return settings;
    }
    private static JsonObject stack(MinecraftClient client,ItemStack stack) {
        JsonObject result=new JsonObject();result.addProperty("id",stack.isEmpty()?"":Registries.ITEM.getId(stack.getItem()).toString());result.addProperty("count",stack.getCount());result.addProperty("modelKey",stack.isEmpty()?"":ItemModelExport.modelKey(client,stack));result.addProperty("name",stack.isEmpty()?"":stack.getName().getString());
        result.addProperty("block",stack.getItem() instanceof BlockItem b && BlockGeometryCapture.supported(b.getBlock().getDefaultState())?Registries.BLOCK.getId(b.getBlock()).toString():"");result.addProperty("spawnType",stack.getItem() instanceof SpawnEggItem egg?Registries.ENTITY_TYPE.getId(egg.getEntityType(stack)).toString():"");return result;
    }
}

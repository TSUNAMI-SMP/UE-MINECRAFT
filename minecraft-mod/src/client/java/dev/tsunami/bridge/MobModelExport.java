package dev.tsunami.bridge;

import com.google.gson.*;
import dev.tsunami.bridge.mixin.ModelPartMobAccessor;
import dev.tsunami.bridge.mixin.LivingEntityMobAccessor;
import dev.tsunami.bridge.mixin.AgeableMobModelAccessor;
import java.io.IOException;
import java.io.ByteArrayOutputStream;
import java.awt.image.BufferedImage;
import javax.imageio.ImageIO;
import net.minecraft.client.render.entity.model.SheepWoolEntityModel;
import net.minecraft.client.render.entity.model.EntityModelLayers;
import net.minecraft.client.render.entity.state.SheepEntityRenderState;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.*;
import java.util.*;
import java.util.function.Consumer;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.model.ModelPart;
import net.minecraft.client.render.entity.LivingEntityRenderer;
import net.minecraft.client.render.entity.model.EntityModel;
import net.minecraft.client.render.entity.state.LivingEntityRenderState;
import net.minecraft.client.texture.NativeImage;
import net.minecraft.client.util.math.MatrixStack;
import org.joml.Vector3f;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.registry.Registries;

/** Captures actual baked model cuboids, UVs and native walk animation of loaded mobs.
 * User resources stay inside the dedicated game's local export directory.
 */
public final class MobModelExport {
    private static final Map<UUID, String> APPEARANCES = new HashMap<>();
    public static final int MAX_MOBS = 128, MAX_PARTS = 256, WALK_FRAMES = 16;
    private MobModelExport() { }
    public static String appearance(UUID id) { return APPEARANCES.get(id); }
    public static void export(MinecraftClient client, Path root, Consumer<String> complete) {
        try { Path path = export(client, root); complete.accept("モブ素材書き出し完了: " + path.toString().replace('\\','/')); }
        catch (IOException | RuntimeException error) { complete.accept("モブ素材書き出し失敗: " + error.getMessage()); }
    }
    public static Path export(MinecraftClient client, Path root) throws IOException {
        if (!client.isOnThread() || client.world == null || client.player == null) throw new IOException("Enter a world; export on the client thread");
        Files.createDirectories(root);
        Path directory = root.resolve("mobs-" + java.time.LocalDateTime.now().toString().replace(':','-') + "-" + UUID.randomUUID().toString().substring(0,8));
        Files.createDirectory(directory);
        JsonObject manifest = new JsonObject(); manifest.addProperty("kind", "mobs"); manifest.addProperty("version", 1);
        JsonObject appearances = new JsonObject(); JsonArray skipped = new JsonArray(); JsonObject entities = new JsonObject();
        JsonObject templates=new JsonObject();
        var eggs=new ArrayList<net.minecraft.item.SpawnEggItem>();net.minecraft.item.SpawnEggItem.getAll().forEach(eggs::add);
        eggs.sort(Comparator.comparingInt(egg -> {
            String type=Registries.ENTITY_TYPE.getId(egg.getEntityType(egg.getDefaultStack())).toString();
            return type.equals("minecraft:zombie") ? 0 : type.equals("minecraft:villager") ? 1 : 2;
        }));
        // Reserve the species defaults before nearby appearance variants can consume the budget.
        for(var egg:eggs) {
            var type=egg.getEntityType(egg.getDefaultStack());String id=Registries.ENTITY_TYPE.getId(type).toString();
            if(MobBridge.unsupportedMovement(id)) {skipped.add(id+" / flight or water gameplay excluded");continue;}
            if(templates.has(id)) continue;
            try {
                // A detached client-side instance is used only to inspect resources. Never add it to a world.
                var entity=type.create(client.world,net.minecraft.entity.SpawnReason.SPAWN_ITEM_USE);
                if(!(entity instanceof MobEntity mob)) throw new IOException("No native ground mob template");
                mob.setPosition(client.player.getX(),client.player.getY(),client.player.getZ());
                String key=exportAppearance(client,mob,directory,appearances);templates.addProperty(id,key);
            } catch(IOException | RuntimeException error) {skipped.add(id+" / template: "+error.getMessage());}
        }
        Map<UUID,String> next = new HashMap<>(); int count = 0;
        for (var entity : client.world.getEntities()) {
            if (!(entity instanceof MobEntity mob) || !mob.isAlive() || mob.squaredDistanceTo(client.player) > 64 * 64) continue;
            if (++count > MAX_MOBS) { skipped.add("nearby_mob_limit_128"); break; }
            try {
                String key=exportAppearance(client,mob,directory,appearances);
                entities.addProperty(mob.getUuidAsString(),key);next.put(mob.getUuid(),key);
            } catch (IOException | RuntimeException error) {
                skipped.add(Registries.ENTITY_TYPE.getId(mob.getType()) + " / " + mob.getUuidAsString() + " / " + error.getMessage());
            }
        }
        manifest.add("templates",templates);
        if(appearances.isEmpty()) throw new IOException("No exportable ground-mob templates or nearby mobs. See exclusions for unsupported models.");
        manifest.add("appearances",appearances); manifest.add("entities",entities); manifest.add("skipped",skipped);
        manifest.addProperty("renderFeatures", "body plus dyed sheep wool; armor, saddles, eyes and other feature layers are not captured");
        Path path=directory.resolve("manifest.json");
        byte[] json=new GsonBuilder().setPrettyPrinting().create().toJson(manifest).getBytes(StandardCharsets.UTF_8);
        if(json.length>32*1024*1024) throw new IOException("Mob geometry exceeds export budget");
        Files.write(path,json,StandardOpenOption.CREATE_NEW); APPEARANCES.clear();APPEARANCES.putAll(next); return path;
    }
    private static String exportAppearance(MinecraftClient client,MobEntity mob,Path directory,JsonObject appearances) throws IOException {
                JsonObject model = capture(client,mob);
                String textureId = model.remove("textureId").getAsString();
                var resource = client.getResourceManager().getResource(net.minecraft.util.Identifier.of(textureId))
                    .orElseThrow(() -> new IOException("Missing active-pack texture: " + textureId));
                byte[] png;
                try (var stream = resource.getInputStream()) { png = stream.readNBytes(4 * 1024 * 1024 + 1); }
                if (png.length > 4 * 1024 * 1024) throw new IOException("Texture too large");
                int width,height;
                try (NativeImage image = NativeImage.read(png)) { width=image.getWidth(); height=image.getHeight(); }
                if(width<1 || height<1 || width>2048 || height>2048) throw new IOException("Texture dimensions exceed 2048");
                if(model.has("woolStart")) {
                    int start=model.remove("woolStart").getAsInt(), tint=model.remove("woolColor").getAsInt();
                    byte[] wool;
                    var woolResource=client.getResourceManager().getResource(net.minecraft.util.Identifier.ofVanilla("textures/entity/sheep/sheep_wool.png"))
                        .orElseThrow(() -> new IOException("Missing active-pack sheep wool texture"));
                    try(var stream=woolResource.getInputStream()) {wool=stream.readNBytes(4*1024*1024+1);}
                    if(wool.length>4*1024*1024) throw new IOException("Wool texture too large");
                    BufferedImage body=ImageIO.read(new java.io.ByteArrayInputStream(png)), layer=ImageIO.read(new java.io.ByteArrayInputStream(wool));
                    if(body==null || layer==null) throw new IOException("Invalid sheep texture");
                    int atlasWidth=Math.max(width,layer.getWidth()), atlasHeight=height+layer.getHeight();
                    if(atlasWidth>2048 || atlasHeight>2048) throw new IOException("Sheep texture atlas exceeds 2048");
                    BufferedImage atlas=new BufferedImage(atlasWidth,atlasHeight,BufferedImage.TYPE_INT_ARGB);
                    for(int y=0;y<height;y++) for(int x=0;x<width;x++) atlas.setRGB(x,y,body.getRGB(x,y));
                    for(int y=0;y<layer.getHeight();y++) for(int x=0;x<layer.getWidth();x++) atlas.setRGB(x,height+y,MobTextureAtlas.tint(layer.getRGB(x,y),tint));
                    JsonArray parts=model.getAsJsonArray("parts");
                    for(int i=0;i<parts.size();i++) for(var quad:parts.get(i).getAsJsonObject().getAsJsonArray("quads")) for(var vertex:quad.getAsJsonArray()) {
                        var v=vertex.getAsJsonArray(); boolean outer=i>=start;
                        v.set(3,new JsonPrimitive(MobTextureAtlas.u(v.get(3).getAsDouble(),outer?layer.getWidth():width,atlasWidth)));
                        v.set(4,new JsonPrimitive(MobTextureAtlas.v(v.get(4).getAsDouble(),outer?layer.getHeight():height,outer?height:0,atlasHeight)));
                    }
                    ByteArrayOutputStream encoded=new ByteArrayOutputStream();ImageIO.write(atlas,"png",encoded);png=encoded.toByteArray();width=atlasWidth;height=atlasHeight;
                }
                String textureHash = sha256(png); model.addProperty("textureHash",textureHash);
                model.addProperty("textureWidth",width); model.addProperty("textureHeight",height);
                String key = sha256(model.toString().getBytes(StandardCharsets.UTF_8));
                String filename = "textures/" + textureHash + ".png";
                model.addProperty("texture",filename);
                if(!appearances.has(key)) {
                    if(appearances.size()>=128) throw new IOException("Appearance limit 128");
                    Files.createDirectories(directory.resolve("textures"));
                    if(!Files.exists(directory.resolve(filename))) Files.write(directory.resolve(filename),png,StandardOpenOption.CREATE_NEW);
                    appearances.add(key,model);
                }
                return key;
    }
    static String sha256(byte[] bytes) {
        try { return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes)); }
        catch(NoSuchAlgorithmException impossible) { throw new IllegalStateException(impossible); }
    }
    @SuppressWarnings({"rawtypes", "unchecked"})
    private static JsonObject capture(MinecraftClient client, MobEntity mob) throws IOException {
        var renderer=client.getEntityRenderDispatcher().getRenderer(mob);
        if(!(renderer instanceof LivingEntityRenderer living)) throw new IOException("No baked living model renderer");
        LivingEntityRenderState state=(LivingEntityRenderState)living.getAndUpdateRenderState(mob,1f);
        // Ageable renderers select the model inside render(), not updateRenderState().
        // Never capture whichever shared adult/baby model happened to render last.
        EntityModel model=living instanceof AgeableMobModelAccessor ageable
            ? (state.baby ? ageable.bridgeBabyModel() : ageable.bridgeAdultModel()) : living.getModel();
        ModelPart root=model.getRootPart();
        SheepWoolEntityModel wool=null;int woolColor=0xffffff;
        if(state instanceof SheepEntityRenderState sheep && !sheep.sheared && !sheep.invisible) {
            wool=new SheepWoolEntityModel(client.getLoadedEntityModels().getModelPart(state.baby ? EntityModelLayers.SHEEP_BABY_WOOL : EntityModelLayers.SHEEP_WOOL));
            woolColor=sheep.getRgbColor();
        }
        state.deathTime=0;state.hurt=false;state.relativeHeadYaw=0;state.pitch=0;
        state.limbSwingAmplitude=0;state.limbSwingAnimationProgress=0;state.age=0;
        model.setAngles(state);
        JsonArray parts=new JsonArray();List<ModelPart> nodes=new ArrayList<>();
        capturePart(root,"root",-1,parts,nodes,true);
        int woolStart=parts.size();
        if(wool!=null) {wool.setAngles((SheepEntityRenderState)state);capturePart(wool.getRootPart(),"wool.root",-1,parts,nodes,true);}
        if(parts.asList().stream().allMatch(part -> part.getAsJsonObject().getAsJsonArray("quads").isEmpty())) throw new IOException("Native model has no visible cuboids");
        JsonArray frames=new JsonArray();
        for(int i=0;i<WALK_FRAMES;i++) {
            state.limbSwingAmplitude=1f;state.limbSwingAnimationProgress=(float)(i*Math.PI*2/WALK_FRAMES / 0.6662);
            state.age=i*2f;model.setAngles(state);if(wool!=null) wool.setAngles((SheepEntityRenderState)state);JsonArray frame=new JsonArray();
            for(ModelPart node:nodes) frame.add(transform(node));frames.add(frame);
        }
        // Restore the shared renderer model using a freshly sampled actual entity state.
        model.setAngles(living.getAndUpdateRenderState(mob,1f));
        JsonObject result=new JsonObject();result.addProperty("type",Registries.ENTITY_TYPE.getId(mob.getType()).toString());
        result.addProperty("textureId",living.getTexture(state).toString());
        MatrixStack rendererScale=new MatrixStack();rendererScale.scale(state.baseScale,state.baseScale,state.baseScale);
        ((LivingEntityMobAccessor)living).bridgeMobScale(state,rendererScale);
        Vector3f scale=rendererScale.peek().getPositionMatrix().getScale(new Vector3f());
        Vector3f offset=rendererScale.peek().getPositionMatrix().getTranslation(new Vector3f());
        JsonArray scales=new JsonArray();scales.add(scale.x);scales.add(scale.y);scales.add(scale.z);result.add("rendererScale",scales);
        JsonArray offsets=new JsonArray();offsets.add(offset.x);offsets.add(offset.y);offsets.add(offset.z);result.add("rendererOffset",offsets);
        JsonObject stats=new JsonObject();stats.addProperty("width",mob.getWidth());stats.addProperty("height",mob.getHeight());stats.addProperty("maxHealth",mob.getMaxHealth());
        stats.addProperty("hostile",mob instanceof net.minecraft.entity.mob.HostileEntity);stats.addProperty("baby",mob.isBaby());
        stats.addProperty("speed",mob.getAttributes().hasAttribute(net.minecraft.entity.attribute.EntityAttributes.MOVEMENT_SPEED) ? mob.getAttributeValue(net.minecraft.entity.attribute.EntityAttributes.MOVEMENT_SPEED) : .25);
        stats.addProperty("knockbackResistance",mob.getAttributeValue(net.minecraft.entity.attribute.EntityAttributes.KNOCKBACK_RESISTANCE));
        stats.addProperty("damage",mob.getAttributes().hasAttribute(net.minecraft.entity.attribute.EntityAttributes.ATTACK_DAMAGE) ? mob.getAttributeValue(net.minecraft.entity.attribute.EntityAttributes.ATTACK_DAMAGE) : 0);
        if(wool!=null) {result.addProperty("woolStart",woolStart);result.addProperty("woolColor",woolColor);}
        result.add("stats",stats);result.add("parts",parts);result.add("walkFrames",frames);return result;
    }
    private static void capturePart(ModelPart node,String name,int parent,JsonArray parts,List<ModelPart> nodes,boolean ancestorsVisible) throws IOException {
        if(nodes.size()>=MAX_PARTS) throw new IOException("Model part limit exceeded");
        int index=nodes.size();nodes.add(node);
        JsonObject part=new JsonObject();part.addProperty("name",name);part.addProperty("parent",parent);
        part.add("transform",transform(node));JsonArray faces=new JsonArray();
        boolean visible=ancestorsVisible && node.visible;
        if(visible && !node.hidden) for(var cuboid:((ModelPartMobAccessor)(Object)node).bridgeMobCuboids()) for(var face:cuboid.sides) {
            JsonArray quad=new JsonArray();for(var vertex:face.vertices()) {
                JsonArray v=new JsonArray();v.add(vertex.x());v.add(vertex.y());v.add(vertex.z());v.add(vertex.u());v.add(vertex.v());quad.add(v);
            }
            faces.add(quad);
        }
        part.add("quads",faces);parts.add(part);
        for(var child:((ModelPartMobAccessor)(Object)node).bridgeMobChildren().entrySet()) capturePart(child.getValue(),child.getKey(),index,parts,nodes,visible);
    }
    private static JsonArray transform(ModelPart part) {
        JsonArray t=new JsonArray();t.add(part.originX);t.add(part.originY);t.add(part.originZ);
        t.add(part.pitch);t.add(part.yaw);t.add(part.roll);t.add(part.xScale);t.add(part.yScale);t.add(part.zScale);return t;
    }
}

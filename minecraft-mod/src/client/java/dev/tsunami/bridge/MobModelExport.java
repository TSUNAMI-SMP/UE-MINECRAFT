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
import net.minecraft.client.render.entity.state.BatEntityRenderState;
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
        manifest.addProperty("renderFeatures", "body and original model feature draw commands: clothing, professions, wool, collars, saddles, armor and eyes; item/beam effects are separate");
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
                if(model.has("featureLayers")) {
                    JsonArray layers=model.remove("featureLayers").getAsJsonArray();int bodyEnd=model.remove("bodyEnd").getAsInt();
                    BufferedImage body=ImageIO.read(new java.io.ByteArrayInputStream(png));
                    if(body==null) throw new IOException("Invalid mob body texture");
                    List<BufferedImage> images=new ArrayList<>();images.add(body);int atlasWidth=width,atlasHeight=height;
                    for(var value:layers) {
                        JsonObject layer=value.getAsJsonObject();
                        var resourceLayer=client.getResourceManager().getResource(net.minecraft.util.Identifier.of(layer.get("texture").getAsString()))
                            .orElseThrow(()->new IOException("Missing mob feature texture: "+layer.get("texture")));
                        byte[] bytes;try(var stream=resourceLayer.getInputStream()) {bytes=stream.readNBytes(4*1024*1024+1);}
                        if(bytes.length>4*1024*1024) throw new IOException("Mob feature texture byte limit");
                        BufferedImage image=ImageIO.read(new java.io.ByteArrayInputStream(bytes));
                        if(image==null || image.getWidth()>2048 || image.getHeight()>2048) throw new IOException("Invalid mob feature texture");
                        images.add(image);atlasWidth=Math.max(atlasWidth,image.getWidth());atlasHeight+=image.getHeight();
                    }
                    if(atlasWidth>2048 || atlasHeight>2048) throw new IOException("Mob feature atlas exceeds 2048");
                    BufferedImage atlas=new BufferedImage(atlasWidth,atlasHeight,BufferedImage.TYPE_INT_ARGB);int offset=0;
                    JsonArray parts=model.getAsJsonArray("parts");
                    for(int index=0;index<images.size();index++) {
                        BufferedImage image=images.get(index);JsonObject layer=index==0 ? null : layers.get(index-1).getAsJsonObject();
                        int tint=layer==null ? 0xffffff : layer.get("color").getAsInt();
                        for(int y=0;y<image.getHeight();y++) for(int x=0;x<image.getWidth();x++) atlas.setRGB(x,offset+y,MobTextureAtlas.tint(image.getRGB(x,y),tint));
                        int first=layer==null ? 0 : layer.get("first").getAsInt(),end=layer==null ? bodyEnd : layer.get("end").getAsInt();
                        for(int p=first;p<end;p++) for(var quad:parts.get(p).getAsJsonObject().getAsJsonArray("quads")) for(var vertex:quad.getAsJsonArray()) {
                            var v=vertex.getAsJsonArray();v.set(3,new JsonPrimitive(MobTextureAtlas.u(v.get(3).getAsDouble(),image.getWidth(),atlasWidth)));
                            v.set(4,new JsonPrimitive(MobTextureAtlas.v(v.get(4).getAsDouble(),image.getHeight(),offset,atlasHeight)));
                        }
                        offset+=image.getHeight();
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
        // Original feature renderers select baby wool and all clothing layers.
        state.deathTime=0;state.hurt=false;state.relativeHeadYaw=0;state.pitch=0;
        state.limbSwingAmplitude=0;state.limbSwingAnimationProgress=0;state.age=0;
        // Bat animations run by age, independently of walking speed. Capture
        // the actual renderer's roost pose and one complete 0.5 s flying loop.
        if(state instanceof BatEntityRenderState bat) {bat.roosting=true;bat.flyingAnimationState.stop();bat.roostingAnimationState.start(0);}
        model.setAngles(state);
        JsonArray parts=new JsonArray();List<ModelPart> nodes=new ArrayList<>();
        capturePart(root,"root",-1,parts,nodes,true);
        int bodyEnd=parts.size();var featureRest=MobFeatureCapture.capture(living,state);
        for(var value:featureRest.parts) {var part=value.getAsJsonObject();MobFeatureCapture.separateOverlay(part);int parent=part.get("parent").getAsInt();if(parent>=0) part.addProperty("parent",parent+bodyEnd);parts.add(part);}
        if(parts.size()>MAX_PARTS) throw new IOException("Body plus feature part budget exceeded");
        if(parts.asList().stream().allMatch(part -> part.getAsJsonObject().getAsJsonArray("quads").isEmpty())) throw new IOException("Native model has no visible cuboids");
        JsonArray frames=new JsonArray();
        if(state instanceof BatEntityRenderState bat) {bat.roosting=false;bat.roostingAnimationState.stop();bat.flyingAnimationState.start(0);}
        for(int i=0;i<WALK_FRAMES;i++) {
            state.limbSwingAmplitude=1f;state.limbSwingAnimationProgress=(float)(i*Math.PI*2/WALK_FRAMES / 0.6662);
            state.age=state instanceof BatEntityRenderState ? i*10f/WALK_FRAMES : i*2f;model.setAngles(state);JsonArray frame=new JsonArray();
            for(ModelPart node:nodes) frame.add(transform(node));
            var features=MobFeatureCapture.capture(living,state);
            if(features.transforms.size()!=featureRest.transforms.size()) throw new IOException("Feature topology changes across walk animation");
            features.transforms.forEach(frame::add);frames.add(frame);
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
        stats.addProperty("armor",mob.getAttributeValue(net.minecraft.entity.attribute.EntityAttributes.ARMOR));
        stats.addProperty("armorToughness",mob.getAttributeValue(net.minecraft.entity.attribute.EntityAttributes.ARMOR_TOUGHNESS));
        stats.addProperty("damage",mob.getAttributes().hasAttribute(net.minecraft.entity.attribute.EntityAttributes.ATTACK_DAMAGE) ? mob.getAttributeValue(net.minecraft.entity.attribute.EntityAttributes.ATTACK_DAMAGE) : 0);
        if(!featureRest.layers.isEmpty()) {
            for(var value:featureRest.layers) {var layer=value.getAsJsonObject();layer.addProperty("first",layer.get("first").getAsInt()+bodyEnd);layer.addProperty("end",layer.get("end").getAsInt()+bodyEnd);}
            result.add("featureLayers",featureRest.layers);result.addProperty("bodyEnd",bodyEnd);
        }
        result.add("stats",stats);result.add("parts",parts);result.add("walkFrames",frames);return result;
    }
    static void capturePart(ModelPart node,String name,int parent,JsonArray parts,List<ModelPart> nodes,boolean ancestorsVisible) throws IOException {
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

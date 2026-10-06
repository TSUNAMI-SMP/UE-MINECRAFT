package dev.tsunami.bridge;

import com.google.gson.*;
import dev.tsunami.bridge.mixin.ModelPartMobAccessor;
import dev.tsunami.bridge.mixin.LivingEntityMobAccessor;
import dev.tsunami.bridge.mixin.AgeableMobModelAccessor;
import java.io.IOException;
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
        Map<UUID,String> next = new HashMap<>(); int count = 0;
        for (var entity : client.world.getEntities()) {
            if (!(entity instanceof MobEntity mob) || !mob.isAlive() || mob.squaredDistanceTo(client.player) > 64 * 64) continue;
            if (++count > MAX_MOBS) { skipped.add("nearby_mob_limit_128"); break; }
            try {
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
                String textureHash = sha256(png); model.addProperty("textureHash",textureHash);
                model.addProperty("textureWidth",width); model.addProperty("textureHeight",height);
                String key = sha256(model.toString().getBytes(StandardCharsets.UTF_8));
                String filename = "textures/" + textureHash + ".png";
                model.addProperty("texture",filename);
                if(!appearances.has(key)) {
                    Files.createDirectories(directory.resolve("textures"));
                    if(!Files.exists(directory.resolve(filename))) Files.write(directory.resolve(filename),png,StandardOpenOption.CREATE_NEW);
                    appearances.add(key,model);
                }
                entities.addProperty(mob.getUuidAsString(),key); next.put(mob.getUuid(),key);
            } catch (IOException | RuntimeException error) {
                skipped.add(Registries.ENTITY_TYPE.getId(mob.getType()) + " / " + mob.getUuidAsString() + " / " + error.getMessage());
            }
        }
        if(appearances.isEmpty()) throw new IOException("No exportable nearby mobs. Spawn animals/enemies within 64 blocks and retry.");
        manifest.add("appearances",appearances); manifest.add("entities",entities); manifest.add("skipped",skipped);
        manifest.addProperty("renderFeatures", "body-model only; armor, saddles, wool/eyes and other feature layers are not captured");
        Path path=directory.resolve("manifest.json");
        byte[] json=new GsonBuilder().setPrettyPrinting().create().toJson(manifest).getBytes(StandardCharsets.UTF_8);
        if(json.length>32*1024*1024) throw new IOException("Mob geometry exceeds export budget");
        Files.write(path,json,StandardOpenOption.CREATE_NEW); APPEARANCES.clear();APPEARANCES.putAll(next); return path;
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
        state.deathTime=0;state.hurt=false;state.relativeHeadYaw=0;state.pitch=0;
        state.limbSwingAmplitude=0;state.limbSwingAnimationProgress=0;state.age=0;
        model.setAngles(state);
        JsonArray parts=new JsonArray();List<ModelPart> nodes=new ArrayList<>();
        capturePart(root,"root",-1,parts,nodes,true);
        JsonArray frames=new JsonArray();
        for(int i=0;i<WALK_FRAMES;i++) {
            state.limbSwingAmplitude=0.8f;state.limbSwingAnimationProgress=(float)(i*Math.PI*2/WALK_FRAMES / 0.6662);
            state.age=i*2f;model.setAngles(state);JsonArray frame=new JsonArray();
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
        result.add("parts",parts);result.add("walkFrames",frames);return result;
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

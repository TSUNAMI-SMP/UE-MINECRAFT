package dev.tsunami.bridge;

import com.google.gson.*;
import java.awt.image.BufferedImage;
import java.io.*;
import java.nio.file.*;
import java.util.*;
import javax.imageio.ImageIO;
import net.minecraft.client.MinecraftClient;
import net.minecraft.item.*;
import net.minecraft.registry.Registries;
import net.minecraft.resource.Resource;
import net.minecraft.resource.ResourceManager;
import net.minecraft.util.Identifier;

/** Copies active HUD resources and bakes native GUI item transforms, yielding between icons. */
public final class NativeUiExport {
    private final Path directory;private final JsonObject manifest=new JsonObject(),sprites=new JsonObject(),textures=new JsonObject(),excluded=new JsonObject();
    private final JsonArray items=new JsonArray(),groups=new JsonArray(),deathPoofFrames=new JsonArray();private final List<ItemStack> stacks=new ArrayList<>();private final Set<Integer> characters=new TreeSet<>();
    private final Map<String,String> cache=new HashMap<>();private final Map<String,BufferedImage> decoded=new HashMap<>();private final long[] written={0};private int cursor;
    private final Iterator<Map.Entry<Identifier,net.minecraft.resource.Resource>> spriteResources;
    private boolean copiedSprites;private final boolean uniform,japanese;
    public NativeUiExport(MinecraftClient client,Path root) throws IOException {
        directory=root.resolve("ui");Files.createDirectory(directory);Files.createDirectory(directory.resolve("textures"));Files.createDirectory(directory.resolve("sprites"));Files.createDirectory(directory.resolve("items"));
        manifest.addProperty("kind","native-ui");manifest.addProperty("version",1);manifest.addProperty("language",client.getLanguageManager().getLanguage());manifest.addProperty("exportId",UUID.randomUUID().toString());uniform=client.options.getForceUnicodeFont().getValue();japanese=client.options.getJapaneseGlyphVariants().getValue();
        var resources=collectSprites(client.getResourceManager());
        for(Identifier id:collectDeathPoof(client.getResourceManager(),resources)) deathPoofFrames.add(spriteKey(id));
        for(int cp=32;cp<=126;cp++) characters.add(cp);
        "照明感度下上視点一人称後前所持品へ検索へアイテムを削除".codePoints().forEach(characters::add);
        "クリエイティブサバイバルインベントリ検索完了読み込み中設定戻る終了経験値保存再開操作アイテムゲームメニューワールド開始地点に所持品クラフトは未対応です".codePoints().forEach(characters::add);
        for(Item item:Registries.ITEM) {ItemStack stack=item.getDefaultStack();if(stack.isEmpty()) continue;if(stacks.size()>=4096) throw new IOException("UI item limit exceeded");stacks.add(stack);stack.getName().getString().codePoints().forEach(characters::add);}
        // Use the actual enabled-feature/operator ItemGroups display order, never registry/alphabetical order.
        if(client.world!=null && client.player!=null) {
            ItemGroups.updateDisplayContext(client.world.getEnabledFeatures(),client.player.isCreativeLevelTwoOp() && client.options.getOperatorItemsTab().getValue(),client.world.getRegistryManager());
            for(ItemGroup group:ItemGroups.getGroupsToDisplay()) {
                if(group.getType()==ItemGroup.Type.HOTBAR) continue; // Saved toolbars require stack-component persistence.
                client.getResourceManager().getResource(group.getTexture()).ifPresent(value->resources.put(group.getTexture(),value));
                JsonObject entry=new JsonObject();entry.addProperty("id",Registries.ITEM_GROUP.getId(group).toString());
                entry.addProperty("name",group.getDisplayName().getString());entry.addProperty("type",group.getType().name().toLowerCase(Locale.ROOT));
                entry.addProperty("row",group.getRow()==ItemGroup.Row.TOP ? 0 : 1);entry.addProperty("column",group.getColumn());entry.addProperty("special",group.isSpecial());
                entry.addProperty("icon",Registries.ITEM.getId(group.getIcon().getItem()).toString());entry.addProperty("texture",spriteKey(group.getTexture()));
                entry.addProperty("scrollbar",group.hasScrollbar());entry.addProperty("renderName",group.shouldRenderName());
                JsonArray ordered=new JsonArray();Set<String> seen=new LinkedHashSet<>();
                for(ItemStack stack:group.getDisplayStacks()) {String id=Registries.ITEM.getId(stack.getItem()).toString();if(seen.add(id)) ordered.add(id);}
                entry.add("items",ordered);entry.addProperty("variantCount",group.getDisplayStacks().size());groups.add(entry);
                group.getDisplayName().getString().codePoints().forEach(characters::add);
            }
        }
        if(resources.size()>2048) throw new IOException("HUD sprite limit 2048 exceeded");spriteResources=resources.entrySet().iterator();
    }
    public int completed() {return cursor;}
    public int total() {return stacks.size();}
    public boolean complete() {return copiedSprites && cursor==stacks.size();}
    public Path directory() {return directory;}
    public void advance(MinecraftClient client,long deadline) throws IOException {
        if(!copiedSprites) {
            do {if(!spriteResources.hasNext()) {copiedSprites=true;break;}
                var entry=spriteResources.next();sprites.add(spriteKey(entry.getKey()),copySprite(directory,entry.getValue(),written));
            } while(System.nanoTime()<deadline);
            return;
        }
        do {
            if(cursor==stacks.size()) return;ItemStack stack=stacks.get(cursor++);String id=Registries.ITEM.getId(stack.getItem()).toString();
            try {
                JsonArray faces=ItemModelExport.guiFaces(client,stack,directory,textures,written,cache);
                for(var face:faces) {String texture=face.getAsJsonObject().get("texture").getAsString();if(!decoded.containsKey(texture)) decoded.put(texture,ImageIO.read(directory.resolve(textures.getAsJsonObject(texture).get("file").getAsString()).toFile()));}
                BufferedImage icon=NativeIconRaster.render(faces,decoded,32);ByteArrayOutputStream out=new ByteArrayOutputStream();ImageIO.write(icon,"png",out);byte[] bytes=out.toByteArray();
                String hash=MobModelExport.sha256(bytes),file="items/"+hash+".png";if(!Files.exists(directory.resolve(file))) Files.write(directory.resolve(file),bytes,StandardOpenOption.CREATE_NEW);
                JsonObject entry=new JsonObject();entry.addProperty("id",id);entry.addProperty("name",stack.getName().getString());entry.addProperty("icon",file);entry.addProperty("sha256",hash);
                entry.addProperty("width",32);entry.addProperty("height",32);entry.addProperty("maxCount",stack.getMaxCount());entry.addProperty("modelKey",ItemModelExport.modelKey(client,stack));
                String block="";if(stack.getItem() instanceof BlockItem b && BlockGeometryCapture.supported(b.getBlock().getDefaultState())) block=Registries.BLOCK.getId(b.getBlock()).toString();entry.addProperty("block",block);
                if(stack.getItem() instanceof SpawnEggItem egg) entry.addProperty("spawnType",Registries.ENTITY_TYPE.getId(egg.getEntityType(stack)).toString());
                var modifiers=stack.getOrDefault(net.minecraft.component.DataComponentTypes.ATTRIBUTE_MODIFIERS,net.minecraft.component.type.AttributeModifiersComponent.DEFAULT);
                entry.addProperty("attackDamage",modifiers.applyOperations(net.minecraft.entity.attribute.EntityAttributes.ATTACK_DAMAGE,1,net.minecraft.entity.EquipmentSlot.MAINHAND));
                entry.addProperty("attackSpeed",modifiers.applyOperations(net.minecraft.entity.attribute.EntityAttributes.ATTACK_SPEED,4,net.minecraft.entity.EquipmentSlot.MAINHAND));
                var equippable=stack.get(net.minecraft.component.DataComponentTypes.EQUIPPABLE);
                var equipmentSlot=equippable==null ? null : equippable.slot();
                int equipment=equipmentSlot==null ? 0 : switch(equipmentSlot) {case HEAD->1;case CHEST->2;case LEGS->3;case FEET->4;default->0;};
                entry.addProperty("equipmentSlot",equipment);
                if(equipment>0) {
                    entry.addProperty("armor",modifiers.applyOperations(net.minecraft.entity.attribute.EntityAttributes.ARMOR,0,equipmentSlot));
                    entry.addProperty("armorToughness",modifiers.applyOperations(net.minecraft.entity.attribute.EntityAttributes.ARMOR_TOUGHNESS,0,equipmentSlot));
                    entry.addProperty("armorKnockbackResistance",modifiers.applyOperations(net.minecraft.entity.attribute.EntityAttributes.KNOCKBACK_RESISTANCE,0,equipmentSlot));
                }
                items.add(entry);
            } catch(RuntimeException | IOException error) {excluded.addProperty(id,error.getMessage()==null ? error.getClass().getSimpleName() : error.getMessage());}
            // Icon source pixels are re-read as needed; bound the decoded cache independently of registry size.
            if(decoded.size()>64) decoded.clear();
        } while(System.nanoTime()<deadline);
    }
    public Path finish(MinecraftClient client,NativeFontExport.Resources resources) throws IOException {
        if(!complete()) throw new IOException("UI export is still in progress");
        if(!sprites.has("hud/hotbar") || !sprites.has("hud/hotbar_selection") || !sprites.has("hud/crosshair")) throw new IOException("Required active-pack HUD sprites missing");
        manifest.add("sprites",sprites);manifest.add("items",items);manifest.add("excludedItems",excluded);
        // Remove icons excluded by the rasterizer so every imported category reference resolves.
        Set<String> exported=new HashSet<>();for(var item:items) exported.add(item.getAsJsonObject().get("id").getAsString());
        for(var value:groups) {JsonObject group=value.getAsJsonObject();JsonArray kept=new JsonArray();for(var id:group.getAsJsonArray("items")) if(exported.contains(id.getAsString())) kept.add(id);group.add("items",kept);}
        manifest.add("groups",groups);manifest.add("deathPoofFrames",deathPoofFrames);manifest.addProperty("catalogueCapture","ItemGroups enabled-feature order; variants with different stack components share the first item ID (component variants are not yet persisted)");
        manifest.add("font",NativeFontExport.export(resources,directory,characters,uniform,japanese));
        manifest.addProperty("iconCapture","Native GUI model/UV/tint snapshot; orthographic CPU rasterization, no glint or animated shader effects");
        Path output=directory.resolve("manifest.json");Files.writeString(output,new GsonBuilder().setPrettyPrinting().create().toJson(manifest),StandardOpenOption.CREATE_NEW);return output;
    }
    static List<String> additionalSprites() {
        // 1.21.11 split the moon atlas into individual celestial phase textures.
        // Query the active ResourceManager so selected packs remain authoritative.
        return List.of("minecraft:textures/gui/container/inventory.png","minecraft:textures/gui/container/creative_inventory/tab_items.png",
                "minecraft:textures/gui/container/creative_inventory/tab_item_search.png","minecraft:textures/gui/container/creative_inventory/tab_inventory.png","minecraft:textures/gui/container/creative_inventory/tabs.png",
                "minecraft:textures/environment/celestial/sun.png",
                "minecraft:textures/environment/celestial/moon/full_moon.png","minecraft:textures/environment/celestial/moon/waning_gibbous.png",
                "minecraft:textures/environment/celestial/moon/third_quarter.png","minecraft:textures/environment/celestial/moon/waning_crescent.png",
                "minecraft:textures/environment/celestial/moon/new_moon.png","minecraft:textures/environment/celestial/moon/waxing_crescent.png",
                "minecraft:textures/environment/celestial/moon/first_quarter.png","minecraft:textures/environment/celestial/moon/waxing_gibbous.png",
                "minecraft:textures/environment/sun.png","minecraft:textures/environment/moon_phases.png");
    }
    static Map<Identifier,Resource> collectSprites(ResourceManager manager) {
        var result=new TreeMap<Identifier,Resource>(Comparator.comparing(Identifier::toString));
        result.putAll(manager.findResources("textures/gui/sprites",id->id.getPath().endsWith(".png")));
        result.putAll(manager.findResources("textures/particle",id->id.getPath().matches("textures/particle/explosion(_[0-9]+)?\\.png")));
        for(String id:additionalSprites()) {
            var key=Identifier.of(id);manager.getResource(key).ifPresent(value->result.put(key,value));
        }
        return result;
    }
    /** SpriteProvider list from the active particle JSON, preserving pack order and repeated frames. */
    static List<Identifier> collectDeathPoof(ResourceManager manager,Map<Identifier,Resource> sprites) throws IOException {
        var definition=manager.getResource(Identifier.ofVanilla("particles/poof.json"));if(definition.isEmpty()) return List.of();
        JsonElement data;try(var input=definition.get().getInputStream()) {
            byte[] bytes=input.readNBytes(65537);if(bytes.length>65536) throw new IOException("Poof sprite definition exceeds budget");
            try {data=JsonParser.parseString(new String(bytes,java.nio.charset.StandardCharsets.UTF_8));} catch(RuntimeException error) {throw new IOException("Invalid poof sprite definition",error);}
        }
        if(!data.isJsonObject() || !data.getAsJsonObject().has("textures") || !data.getAsJsonObject().get("textures").isJsonArray()) throw new IOException("Poof sprite definition has no textures");
        JsonArray textures=data.getAsJsonObject().getAsJsonArray("textures");if(textures.size()>256) throw new IOException("Poof sprite frame limit exceeded");
        List<Identifier> ordered=new ArrayList<>();
        for(var value:textures) {
            if(!value.isJsonPrimitive() || !value.getAsJsonPrimitive().isString()) throw new IOException("Invalid poof sprite ID");
            Identifier texture=Identifier.tryParse(value.getAsString());if(texture==null || texture.getPath().contains("..")) throw new IOException("Invalid poof sprite ID");
            Identifier file=Identifier.of(texture.getNamespace(),"textures/particle/"+texture.getPath()+".png");
            Resource resource=manager.getResource(file).orElseThrow(()->new IOException("Missing active poof sprite: "+file));sprites.put(file,resource);ordered.add(file);
        }
        return ordered;
    }
    static String spriteKey(Identifier key) {
        String id=key.getPath();
        if(id.startsWith("textures/gui/sprites/")) id=id.substring("textures/gui/sprites/".length(),id.length()-4);
        else if(id.startsWith("textures/gui/")) id=id.substring("textures/gui/".length(),id.length()-4);
        else id=id.substring("textures/".length(),id.length()-4);
        return key.getNamespace().equals("minecraft") ? id : key.getNamespace()+":"+id;
    }
    static JsonObject copySprite(Path directory,Resource resource,long[] written) throws IOException {
        byte[] bytes;try(var in=resource.getInputStream()) {bytes=in.readNBytes(4*1024*1024+1);}
        if(bytes.length>4*1024*1024) throw new IOException("HUD sprite byte budget exceeded");
        BufferedImage image=decodePng(bytes);String hash=MobModelExport.sha256(bytes),file="sprites/"+hash+".png";
        if(!Files.exists(directory.resolve(file))) Files.write(directory.resolve(file),bytes,StandardOpenOption.CREATE_NEW);
        JsonObject metadata=new JsonObject();metadata.addProperty("file",file);metadata.addProperty("sha256",hash);metadata.addProperty("width",image.getWidth());metadata.addProperty("height",image.getHeight());
        written[0]+=bytes.length;if(written[0]>128L*1024*1024) throw new IOException("UI resource byte budget exceeded");
        return metadata;
    }
    static BufferedImage decodePng(byte[] png) throws IOException {
        try(var in=new javax.imageio.stream.MemoryCacheImageInputStream(new ByteArrayInputStream(png))) {
            var readers=ImageIO.getImageReaders(in);if(!readers.hasNext()) throw new IOException("Invalid PNG");var reader=readers.next();
            try {reader.setInput(in,true,true);int width=reader.getWidth(0),height=reader.getHeight(0);if(width<1 || height<1 || width>4096 || height>4096 || (long)width*height>4_194_304) throw new IOException("UI image dimensions exceed budget");return reader.read(0);}finally {reader.dispose();}
        }
    }
}

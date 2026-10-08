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
    private final JsonArray items=new JsonArray();private final List<ItemStack> stacks=new ArrayList<>();private final Set<Integer> characters=new TreeSet<>();
    private final Map<String,String> cache=new HashMap<>();private final Map<String,BufferedImage> decoded=new HashMap<>();private final long[] written={0};private int cursor;
    private final Iterator<Map.Entry<Identifier,net.minecraft.resource.Resource>> spriteResources;
    private boolean copiedSprites;private final boolean uniform,japanese;
    public NativeUiExport(MinecraftClient client,Path root) throws IOException {
        directory=root.resolve("ui");Files.createDirectory(directory);Files.createDirectory(directory.resolve("textures"));Files.createDirectory(directory.resolve("sprites"));Files.createDirectory(directory.resolve("items"));
        manifest.addProperty("kind","native-ui");manifest.addProperty("version",1);manifest.addProperty("language",client.getLanguageManager().getLanguage());manifest.addProperty("exportId",UUID.randomUUID().toString());uniform=client.options.getForceUnicodeFont().getValue();japanese=client.options.getJapaneseGlyphVariants().getValue();
        var resources=collectSprites(client.getResourceManager());
        if(resources.size()>2048) throw new IOException("HUD sprite limit 2048 exceeded");spriteResources=resources.entrySet().iterator();
        for(int cp=32;cp<=126;cp++) characters.add(cp);
        "照明".codePoints().forEach(characters::add);
        "クリエイティブサバイバルインベントリ検索完了読み込み中設定戻る終了経験値保存再開操作アイテムゲームメニューワールド開始地点に所持品クラフトは未対応です".codePoints().forEach(characters::add);
        for(Item item:Registries.ITEM) {ItemStack stack=item.getDefaultStack();if(stack.isEmpty()) continue;if(stacks.size()>=4096) throw new IOException("UI item limit exceeded");stacks.add(stack);stack.getName().getString().codePoints().forEach(characters::add);}
        stacks.sort(Comparator.comparing(stack->Registries.ITEM.getId(stack.getItem()).toString()));
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
        manifest.add("font",NativeFontExport.export(resources,directory,characters,uniform,japanese));
        manifest.addProperty("iconCapture","Native GUI model/UV/tint snapshot; orthographic CPU rasterization, no glint or animated shader effects");
        Path output=directory.resolve("manifest.json");Files.writeString(output,new GsonBuilder().setPrettyPrinting().create().toJson(manifest),StandardOpenOption.CREATE_NEW);return output;
    }
    static List<String> additionalSprites() {
        // 1.21.11 split the moon atlas into individual celestial phase textures.
        // Query the active ResourceManager so selected packs remain authoritative.
        return List.of("minecraft:textures/gui/container/inventory.png","minecraft:textures/gui/container/creative_inventory/tab_items.png",
                "minecraft:textures/gui/container/creative_inventory/tab_item_search.png","minecraft:textures/gui/container/creative_inventory/tabs.png",
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

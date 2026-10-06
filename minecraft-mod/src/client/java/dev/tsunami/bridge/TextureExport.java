package dev.tsunami.bridge;

import com.google.gson.*;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.*;
import java.util.*;
import javax.imageio.ImageIO;
import javax.imageio.stream.MemoryCacheImageInputStream;

/** Portable resource resolver/exporter. No Minecraft objects, no downloaded/distributed assets. */
public final class TextureExport {
    private static final class Unsupported extends IOException { Unsupported(String message) { super(message); } Unsupported(String message, Throwable cause) { super(message,cause); } }
    public interface Resources { byte[] read(String resourceId) throws IOException; }
    public record Block(String id, Map<String,String> properties) {}
    public record Face(String texture, boolean tint) {}
    public record Faces(Face top, Face side, Face bottom) {}
    private record Model(Map<String,String> textures, JsonArray elements) {}
    private final Resources source;
    private final Path directory;
    private final JsonObject textures=new JsonObject(), blocks=new JsonObject();
    private final Map<String,Model> models=new LinkedHashMap<>();
    private long readBytes, writtenBytes;
    private int skipped;
    public TextureExport(Resources source,Path directory) throws IOException {
        this.source=source; this.directory=directory.toAbsolutePath().normalize(); Files.createDirectory(this.directory);
    }
    public static String id(String value) throws IOException {
        String result=value.contains(":") ? value : "minecraft:"+value;
        if (result.length()>160 || !result.matches("[a-z0-9_.-]+:[a-z0-9_./-]+")
                || result.substring(result.indexOf(':')+1).startsWith("/") || Arrays.asList(result.split("[:/]",-1)).contains(".."))
            throw new Unsupported("Invalid resource ID: "+value);
        return result;
    }
    private byte[] read(String name,int limit) throws IOException {
        byte[] bytes=source.read(id(name));
        if(bytes==null || bytes.length>limit) throw new Unsupported("Missing/oversized resource: "+name);
        readBytes+=bytes.length; if(readBytes>256L*1024*1024) throw new IOException("Resource read budget exceeded");
        return bytes;
    }
    private JsonObject json(String name) throws IOException {
        try { return JsonParser.parseString(new String(read(name,256*1024),StandardCharsets.UTF_8)).getAsJsonObject(); }
        catch(RuntimeException e) { throw new Unsupported("Invalid model JSON: "+name,e); }
    }
    private static String resource(String name,String prefix,String suffix) throws IOException {
        String n=id(name); int colon=n.indexOf(':'); return n.substring(0,colon+1)+prefix+n.substring(colon+1)+suffix;
    }
    private Model model(String name,Set<String> chain) throws IOException {
        name=id(name); if(models.containsKey(name)) return models.get(name);
        if(chain.size()>=16 || !chain.add(name)) throw new Unsupported("Cyclic/deep block model");
        JsonObject object=json(resource(name,"models/",".json"));
        Map<String,String> vars=new LinkedHashMap<>(); JsonArray elements=new JsonArray();
        if(object.has("parent") && !object.get("parent").getAsString().startsWith("builtin:")) {
            Model parent=model(object.get("parent").getAsString(),chain); vars.putAll(parent.textures); elements=parent.elements;
        }
        if(object.has("textures")) object.getAsJsonObject("textures").entrySet().forEach(e->vars.put(e.getKey(),e.getValue().getAsString()));
        if(object.has("elements")) elements=object.getAsJsonArray("elements");
        Model result=new Model(vars,elements); chain.remove(name);
        if(models.size()>=512) models.remove(models.keySet().iterator().next()); models.put(name,result); return result;
    }
    private String texture(String value,Map<String,String> vars) throws IOException {
        Set<String> seen=new HashSet<>();
        while(value.startsWith("#")) {
            String key=value.substring(1); if(seen.size()>=16 || !seen.add(key) || !vars.containsKey(key)) throw new Unsupported("Cyclic/missing texture variable");
            value=vars.get(key);
        }
        return id(value);
    }
    private Face face(Model model,String direction) throws IOException {
        for(JsonElement item:model.elements) {
            JsonObject element=item.getAsJsonObject();
            if(!element.has("faces") || !element.getAsJsonObject("faces").has(direction)) continue;
            JsonObject face=element.getAsJsonObject("faces").getAsJsonObject(direction);
            return new Face(texture(face.get("texture").getAsString(),model.textures),face.has("tintindex") && face.get("tintindex").getAsInt()>=0);
        }
        throw new Unsupported("Model has no "+direction+" face");
    }
    public Faces resolve(Block block) throws IOException {
        String blockId=id(block.id); JsonObject state=json(resource(blockId,"blockstates/",".json"));
        if(!state.has("variants")) throw new Unsupported("Multipart/builtin model is not supported yet");
        JsonElement chosen=null; int specificity=-1;
        for(var entry:state.getAsJsonObject("variants").entrySet()) {
            boolean match=true; String key=entry.getKey(); int count=0;
            if(!key.isEmpty()) for(String item:key.split(",")) {
                String[] pair=item.split("=",2); if(pair.length!=2 || !pair[1].equals(block.properties.get(pair[0]))) { match=false; break; } count++;
            }
            if(match && count>specificity) { chosen=entry.getValue(); specificity=count; }
        }
        if(chosen==null) throw new Unsupported("Default block state variant missing");
        if(chosen.isJsonArray()) chosen=chosen.getAsJsonArray().get(0); // Weighted/random variant: first model.
        Model m=model(chosen.getAsJsonObject().get("model").getAsString(),new HashSet<>());
        return new Faces(face(m,"up"),face(m,"north"),face(m,"down"));
    }
    private void exportTexture(String texture) throws IOException {
        if(textures.has(texture)) return;
        if(textures.size()>=4096) throw new IOException("Texture count budget exceeded");
        byte[] raw=read(resource(texture,"textures/",".png"),4*1024*1024);
        byte[] png=raw; int width,height;
        try(var stream=new MemoryCacheImageInputStream(new ByteArrayInputStream(raw))) {
            var readers=ImageIO.getImageReaders(stream); if(!readers.hasNext()) throw new Unsupported("Invalid PNG");
            var reader=readers.next();
            try {
                if(!"PNG".equalsIgnoreCase(reader.getFormatName())) throw new Unsupported("Expected PNG");
                reader.setInput(stream,true,true); width=reader.getWidth(0); height=reader.getHeight(0);
                if(width<1 || height<1 || width>2048 || height>16384 || (long)width*height>16_777_216) throw new Unsupported("Texture dimensions exceed budget");
                // Snapshot the first frame of vertical animated strips. Non-animated rectangular PNGs remain intact.
                byte[] meta;
                try { meta=read(resource(texture,"textures/",".png.mcmeta"),65536); } catch(Unsupported missing) { meta=null; }
                if(meta!=null && height>width) {
                    JsonObject animation=JsonParser.parseString(new String(meta,StandardCharsets.UTF_8)).getAsJsonObject().getAsJsonObject("animation");
                    if(animation!=null) {
                        int fw=animation.has("width") ? animation.get("width").getAsInt() : width;
                        int fh=animation.has("height") ? animation.get("height").getAsInt() : fw;
                        if(fw<1 || fh<1 || fw>width || fh>height) throw new Unsupported("Invalid animation frame size");
                        var image=reader.read(0); var out=new ByteArrayOutputStream();
                        if(!ImageIO.write(image.getSubimage(0,0,fw,fh),"png",out)) throw new IOException("PNG writer unavailable");
                        png=out.toByteArray(); width=fw; height=fh;
                    }
                }
            } finally { reader.dispose(); }
        }
        if(height>2048) throw new Unsupported("Static texture height exceeds UE import budget");
        String n=id(texture); int colon=n.indexOf(':'); String relative="assets/"+n.substring(0,colon)+"/textures/"+n.substring(colon+1)+".png";
        Path target=directory.resolve(relative).normalize(); if(!target.startsWith(directory)) throw new IOException("Texture path escapes export");
        writtenBytes+=png.length; if(writtenBytes>128L*1024*1024) throw new IOException("Texture write budget exceeded");
        Files.createDirectories(target.getParent()); Files.write(target,png,StandardOpenOption.CREATE_NEW);
        JsonObject metadata=new JsonObject(); metadata.addProperty("file",relative); metadata.addProperty("width",width); metadata.addProperty("height",height);
        try { metadata.addProperty("sha256",HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(png))); }
        catch(NoSuchAlgorithmException impossible) { throw new IllegalStateException(impossible); }
        textures.add(texture,metadata);
    }
    private JsonObject faceJson(Face face) { JsonObject p=new JsonObject(); p.addProperty("texture",face.texture); p.addProperty("tint",face.tint); return p; }
    public boolean export(Block block) throws IOException {
        try {
            Faces faces=resolve(block); exportTexture(faces.top.texture); exportTexture(faces.side.texture); exportTexture(faces.bottom.texture);
            JsonObject p=new JsonObject(); p.add("top",faceJson(faces.top)); p.add("side",faceJson(faces.side)); p.add("bottom",faceJson(faces.bottom));
            blocks.add(id(block.id),p); return true;
        } catch(Unsupported | RuntimeException unsupported) { skipped++; return false; }
    }
    public Path finish() throws IOException {
        JsonObject manifest=new JsonObject(); manifest.addProperty("format","uebridge-block-textures"); manifest.addProperty("version",1);
        manifest.addProperty("skipped",skipped); manifest.add("blocks",blocks); manifest.add("textures",textures);
        Path output=directory.resolve("manifest.json"); Files.writeString(output,new GsonBuilder().setPrettyPrinting().create().toJson(manifest),StandardOpenOption.CREATE_NEW); return output;
    }
    public int exported() { return blocks.size(); }
    public int skipped() { return skipped; }
}

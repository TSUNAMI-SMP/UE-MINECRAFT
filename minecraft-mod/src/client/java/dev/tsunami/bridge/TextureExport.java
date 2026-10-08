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
    /** Render colours are sampled from the active world's vanilla BlockColors before the worker starts.
     * Particle tint is independent: grass dust deliberately uses untinted dirt. */
    public record State(Map<String,String> properties,List<double[]> collision,List<double[]> outline,List<String> solidFaces,boolean cannotConnect,Boolean opaqueFullCube,int emission,int opacity) {
        public State(Map<String,String> properties,List<double[]> collision,List<double[]> outline,List<String> solidFaces,boolean cannotConnect) {this(properties,collision,outline,solidFaces,cannotConnect,null,0,15);}
        public State(Map<String,String> properties,List<double[]> collision,List<double[]> outline) { this(properties,collision,outline,null,false); }
    }
    public record Block(String id, Map<String,String> properties, int particleColor,List<State> states,String excludedReason,double horizontalOffset,double verticalOffset,Map<Integer,Integer> renderTints) {
        public Block(String id,Map<String,String> properties,int color,List<State> states,String reason,double horizontal,double vertical) {this(id,properties,color,states,reason,horizontal,vertical,Map.of(0,color));}
        public Block(String id,Map<String,String> properties,int color,List<State> states,String reason) { this(id,properties,color,states,reason,0,0); }
        public Block(String id,Map<String,String> properties,int color) { this(id,properties,color,List.of(),""); }
        public Block(String id,Map<String,String> properties) { this(id,properties,0xffffff); }
    }
    public record Face(String texture, boolean tint) {}
    public record Faces(Face top, Face side, Face bottom, Face particle) {}
    private record Model(Map<String,String> textures, JsonArray elements) {}
    private final Resources source;
    private final Path directory;
    private final JsonObject textures=new JsonObject(), blocks=new JsonObject(), exportedModels=new JsonObject(), blockstates=new JsonObject(), exclusions=new JsonObject();
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
        if(models.size()>=16384) throw new IOException("Model count budget exceeded"); models.put(name,result); return result;
    }
    private String texture(String value,Map<String,String> vars) throws IOException {
        // Vanilla heavy_core uses a bare texture variable rather than '#all'.
        if(!value.contains(":") && !value.contains("/") && vars.containsKey(value)) value="#"+value;
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
        // Crosses/buttons do not have six cube faces. Keep legacy dust/material metadata
        // without pretending their visual model is a cube.
        for(JsonElement item:model.elements) {
            JsonObject element=item.getAsJsonObject(); if(!element.has("faces")) continue;
            for(var entry:element.getAsJsonObject("faces").entrySet()) {
                JsonObject f=entry.getValue().getAsJsonObject();
                return new Face(texture(f.get("texture").getAsString(),model.textures),f.has("tintindex") && f.get("tintindex").getAsInt()>=0);
            }
        }
        throw new Unsupported("Model has no renderable face");
    }
    public Faces resolve(Block block) throws IOException {
        String blockId=id(block.id); JsonObject state=json(resource(blockId,"blockstates/",".json"));
        if(!state.has("variants")) {
            if(!state.has("multipart")) throw new Unsupported("Blockstate model missing");
            for(JsonElement part:state.getAsJsonArray("multipart")) {
                JsonObject p=part.getAsJsonObject(); if(!p.has("when") || matches(p.get("when"),block.properties)) {
                    JsonElement apply=p.get("apply"); if(apply.isJsonArray()) apply=apply.getAsJsonArray().get(0);
                    return faces(block,model(apply.getAsJsonObject().get("model").getAsString(),new HashSet<>()));
                }
            }
            // A default vine/lichen state may have no attached faces. Its other states
            // still have valid models, and dust uses the first available particle sprite.
            if(!state.getAsJsonArray("multipart").isEmpty()) {
                JsonElement apply=state.getAsJsonArray("multipart").get(0).getAsJsonObject().get("apply");
                if(apply.isJsonArray()) apply=apply.getAsJsonArray().get(0);
                return faces(block,model(apply.getAsJsonObject().get("model").getAsString(),new HashSet<>()));
            }
            throw new Unsupported("Empty multipart model");
        }
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
        return faces(block,m);
    }
    private Faces faces(Block block,Model m) throws IOException {
        String blockId=id(block.id);
        Face top=face(m,"up"),side=face(m,"north"),bottom=face(m,"down");
        // Vanilla block dust uses the model's particle sprite, which can differ from all three faces.
        // Grass dust is explicitly untinted in BlockDustParticle, even though its top face is tinted.
        String particleTexture=m.textures.containsKey("particle") ? texture("#particle",m.textures) : side.texture;
        boolean particleTint=!blockId.equals("minecraft:grass_block") && (block.particleColor & 0xffffff)!=0xffffff;
        return new Faces(top,side,bottom,new Face(particleTexture,particleTint));
    }
    private static boolean matches(JsonElement condition,Map<String,String> properties) {
        if(!condition.isJsonObject()) return false;
        JsonObject c=condition.getAsJsonObject();
        if(c.has("OR")) { for(JsonElement item:c.getAsJsonArray("OR")) if(matches(item,properties)) return true; return false; }
        if(c.has("AND")) { for(JsonElement item:c.getAsJsonArray("AND")) if(!matches(item,properties)) return false; return true; }
        for(var e:c.entrySet()) if(!Arrays.asList(e.getValue().getAsString().split("\\|",-1)).contains(properties.get(e.getKey()))) return false;
        return true;
    }
    /** Stable state keys are shared by the manifest and each world row. */
    public static String stateKey(Map<String,String> properties) {
        return properties.entrySet().stream().sorted(Map.Entry.comparingByKey())
            .map(e->e.getKey()+"="+e.getValue()).collect(java.util.stream.Collectors.joining(","));
    }
    /** BlockColors#create: these providers read a positional biome resolver. */
    public static String tintSource(String blockId,int tintIndex) {
        if(tintIndex<0) return "none";
        return switch(blockId) {
            case "minecraft:grass_block", "minecraft:fern", "minecraft:short_grass", "minecraft:potted_fern",
                 "minecraft:bush", "minecraft:large_fern", "minecraft:tall_grass", "minecraft:sugar_cane" -> "grass";
            case "minecraft:pink_petals", "minecraft:wildflowers" -> tintIndex==0 ? "none" : "grass";
            case "minecraft:oak_leaves", "minecraft:jungle_leaves", "minecraft:acacia_leaves", "minecraft:dark_oak_leaves",
                 "minecraft:vine", "minecraft:mangrove_leaves" -> "foliage";
            case "minecraft:leaf_litter" -> "dry_foliage";
            default -> "constant";
        };
    }
    private void bakeReferenced(JsonElement entry) throws IOException {
        if(entry.isJsonArray()) { for(JsonElement e:entry.getAsJsonArray()) bakeReferenced(e); return; }
        JsonObject ref=entry.getAsJsonObject(); String name=id(ref.get("model").getAsString());
        ref.addProperty("model",name);
        if(exportedModels.has(name)) return;
        Model m=model(name,new HashSet<>()); JsonObject out=new JsonObject(); JsonObject vars=new JsonObject();
        for(var e:m.textures.entrySet()) vars.addProperty(e.getKey(),texture(e.getValue(),m.textures));
        JsonArray elements=m.elements.deepCopy();
        if(elements.size()>256) throw new Unsupported("Model element limit exceeded");
        for(JsonElement e:elements) {
            JsonObject element=e.getAsJsonObject(); if(!element.has("faces")) continue;
            for(var f:element.getAsJsonObject("faces").entrySet()) {
                JsonObject face=f.getValue().getAsJsonObject(); String t=texture(face.get("texture").getAsString(),m.textures);
                exportTexture(t); face.addProperty("texture",t);
            }
        }
        out.add("textures",vars); out.add("elements",elements); exportedModels.add(name,out);
    }
    private static JsonArray boxes(List<double[]> values) throws IOException {
        if(values.size()>64) throw new IOException("Voxel shape box limit exceeded");
        JsonArray result=new JsonArray();
        for(double[] box:values) {
            if(box.length!=6) throw new IOException("Invalid voxel box"); JsonArray row=new JsonArray();
            for(double n:box) { if(!Double.isFinite(n)||n < -4 || n>4) throw new IOException("Invalid voxel box coordinate"); row.add(n); }
            for(int axis=0;axis<3;axis++) if(box[axis]>=box[axis+3]) throw new IOException("Empty voxel box"); result.add(row);
        }
        return result;
    }
    private void exportTexture(String texture) throws IOException {
        if(textures.has(texture)) return;
        if(textures.size()>=4096) throw new IOException("Texture count budget exceeded");
        byte[] raw=read(resource(texture,"textures/",".png"),4*1024*1024);
        byte[] png=raw; int width,height;String alphaMode="opaque";boolean fluidTexture=texture.contains("/water_") || texture.contains("/lava_"),shaderInterpolation=false;int frameCount=1,frameTime=2;
        try(var stream=new MemoryCacheImageInputStream(new ByteArrayInputStream(raw))) {
            var readers=ImageIO.getImageReaders(stream); if(!readers.hasNext()) throw new Unsupported("Invalid PNG");
            var reader=readers.next();
            try {
                if(!"PNG".equalsIgnoreCase(reader.getFormatName())) throw new Unsupported("Expected PNG");
                reader.setInput(stream,true,true); width=reader.getWidth(0); height=reader.getHeight(0);
                if(width<1 || height<1 || width>2048 || height>16384 || (long)width*height>16_777_216) throw new Unsupported("Texture dimensions exceed budget");
                var pixels=reader.read(0);
                // Keep the active pack's animation, including functional-block
                // sprites and custom frame lists, rather than freezing frame 0.
                byte[] meta;
                try { meta=read(resource(texture,"textures/",".png.mcmeta"),65536); } catch(Unsupported missing) { meta=null; }
                if(meta!=null) {
                    JsonObject animation=JsonParser.parseString(new String(meta,StandardCharsets.UTF_8)).getAsJsonObject().getAsJsonObject("animation");
                    if(animation!=null) {
                        var strip=TextureAnimation.bake(pixels,animation);pixels=strip.pixels();
                        frameCount=strip.frames();frameTime=strip.ticks();shaderInterpolation=strip.shaderInterpolation();var out=new ByteArrayOutputStream();
                        if(!ImageIO.write(pixels,"png",out)) throw new IOException("PNG writer unavailable");
                        png=out.toByteArray();width=pixels.getWidth();height=pixels.getHeight();
                    }
                }
                boolean cutout=false,translucent=false;
                for(int y=0;y<height && !translucent;y++) for(int x=0;x<width;x++) {
                    int alpha=pixels.getRGB(x,y)>>>24;if(alpha==0) cutout=true;else if(alpha!=255) {translucent=true;break;}
                }
                alphaMode=translucent ? "translucent" : cutout ? "cutout" : "opaque";
            } finally { reader.dispose(); }
        }
        if(height>2048 && frameCount==1) throw new Unsupported("Static texture height exceeds UE import budget");
        String n=id(texture); int colon=n.indexOf(':'); String relative="assets/"+n.substring(0,colon)+"/textures/"+n.substring(colon+1)+".png";
        Path target=directory.resolve(relative).normalize(); if(!target.startsWith(directory)) throw new IOException("Texture path escapes export");
        writtenBytes+=png.length; if(writtenBytes>128L*1024*1024) throw new IOException("Texture write budget exceeded");
        Files.createDirectories(target.getParent()); Files.write(target,png,StandardOpenOption.CREATE_NEW);
        JsonObject metadata=new JsonObject(); metadata.addProperty("file",relative); metadata.addProperty("width",width); metadata.addProperty("height",height);
        metadata.addProperty("alphaMode",alphaMode);
        if(frameCount>1 || fluidTexture) {metadata.addProperty("animationFrames",frameCount);metadata.addProperty("animationFrameTime",frameTime);}
        if(shaderInterpolation) metadata.addProperty("animationInterpolate",true);
        try { metadata.addProperty("sha256",HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(png))); }
        catch(NoSuchAlgorithmException impossible) { throw new IllegalStateException(impossible); }
        textures.add(texture,metadata);
    }
    private JsonObject faceJson(Face face) { JsonObject p=new JsonObject(); p.addProperty("texture",face.texture); p.addProperty("tint",face.tint); return p; }
    public boolean export(Block block) throws IOException {
        try {
            if(!block.excludedReason.isEmpty()) { exclusions.addProperty(id(block.id),block.excludedReason); skipped++; return false; }
            boolean fluid=block.id.equals("minecraft:water") || block.id.equals("minecraft:lava") || block.id.equals("minecraft:bubble_column");
            boolean invisible=Set.of("minecraft:air","minecraft:cave_air","minecraft:void_air","minecraft:barrier","minecraft:light","minecraft:structure_void").contains(block.id);
            boolean fallback=false;Faces faces;
            if(fluid) {String base=block.id.equals("minecraft:lava") ? "minecraft:block/lava_still" : "minecraft:block/water_still";faces=new Faces(new Face(base,!block.id.equals("minecraft:lava")),new Face(base.replace("_still","_flow"),!block.id.equals("minecraft:lava")),new Face(base,!block.id.equals("minecraft:lava")),new Face(base,false));}
            else try {faces=resolve(block);} catch(Unsupported missing) {fallback=true;faces=new Faces(new Face("minecraft:block/stone",false),new Face("minecraft:block/stone",false),new Face("minecraft:block/stone",false),new Face("minecraft:block/stone",false));} exportTexture(faces.top.texture); exportTexture(faces.side.texture); exportTexture(faces.bottom.texture); exportTexture(faces.particle.texture);
            JsonObject p=new JsonObject(); p.add("top",faceJson(faces.top)); p.add("side",faceJson(faces.side)); p.add("bottom",faceJson(faces.bottom));
            JsonObject particle=faceJson(faces.particle);
            particle.addProperty("color",faces.particle.tint ? block.particleColor & 0xffffff : 0xffffff);
            p.add("particle",particle);
            JsonObject renderTints=new JsonObject(),tintSources=new JsonObject();
            for(var tint:block.renderTints.entrySet()) {
                if(tint.getKey()<0 || tint.getKey()>255 || tint.getValue()<0 || tint.getValue()>0xffffff)
                    throw new Unsupported("Invalid native render tint");
                renderTints.addProperty(Integer.toString(tint.getKey()),tint.getValue());
                tintSources.addProperty(Integer.toString(tint.getKey()),tintSource(id(block.id),tint.getKey()));
            }
            p.add("renderTints",renderTints);p.add("tintSources",tintSources);
            p.addProperty("defaultState",stateKey(block.properties));
            JsonArray offset=new JsonArray();offset.add(block.horizontalOffset);offset.add(block.verticalOffset);p.add("modelOffset",offset);
            JsonObject states=new JsonObject();
            if(block.states.size()>8192) throw new Unsupported("Block state count limit exceeded");
            for(State state:block.states) {
                JsonObject data=new JsonObject(); data.add("collision",boxes(state.collision)); data.add("outline",boxes(state.outline));
                if(state.solidFaces!=null) {JsonArray facesJson=new JsonArray();for(String face:state.solidFaces) facesJson.add(face);data.add("solidFaces",facesJson);}
                data.addProperty("cannotConnect",state.cannotConnect);
                if(state.opaqueFullCube!=null) data.addProperty("opaqueFullCube",state.opaqueFullCube);
                data.addProperty("emission",state.emission);data.addProperty("opacity",state.opacity);
                states.add(stateKey(state.properties),data);
            }
            p.add("states",states);
            JsonObject sourceState;
            if(fluid || fallback || invisible) {
                sourceState=new JsonObject();JsonObject variants=new JsonObject();
                for(State state:block.states) {
                    String key=stateKey(state.properties);String modelName="minecraft:block/uebridge_"+(invisible ? "invisible_" : fluid ? "fluid_" : "fallback_")+block.id.substring(block.id.indexOf(':')+1)+"_"+(fluid ? variants.size() : 0);
                    JsonObject model=new JsonObject(),element=new JsonObject();JsonArray from=new JsonArray(),to=new JsonArray();from.add(0);from.add(0);from.add(0);to.add(16);
                    int level=fluid ? Integer.parseInt(state.properties.getOrDefault("level","0")) : 0;
                    double height=fluid ? (level>=8 ? 8./9 : (8-level)/9.) : 1;to.add(height*16);to.add(16);element.add("from",from);element.add("to",to);
                    JsonObject nativeFaces=new JsonObject();
                    for(String side:List.of("up","down","north","south","east","west")) {JsonObject face=new JsonObject();face.addProperty("texture",side.equals("up") ? faces.top.texture : side.equals("down") ? faces.bottom.texture : faces.side.texture);
                        if(fluid && !side.equals("up") && !side.equals("down")) {JsonArray uv=new JsonArray();uv.add(0);uv.add((1-height)*8);uv.add(8);uv.add(8);face.add("uv",uv);}
                        if(fluid && !block.id.equals("minecraft:lava")) face.addProperty("tintindex",0);face.addProperty("cullface",side);nativeFaces.add(side,face);}
                    element.add("faces",nativeFaces);JsonArray elements=new JsonArray();if(!invisible) elements.add(element);model.add("elements",elements);model.add("textures",new JsonObject());exportedModels.add(modelName,model);
                    JsonObject apply=new JsonObject();apply.addProperty("model",modelName);variants.add(key,apply);
                }
                sourceState.add("variants",variants);p.addProperty("itemFallback",fallback && !invisible);
            } else sourceState=json(resource(block.id,"blockstates/",".json"));
            if(!fluid && !fallback && !invisible && sourceState.has("variants")) for(var e:sourceState.getAsJsonObject("variants").entrySet()) bakeReferenced(e.getValue());
            if(!fluid && !fallback && !invisible && sourceState.has("multipart")) for(var e:sourceState.getAsJsonArray("multipart")) bakeReferenced(e.getAsJsonObject().get("apply"));
            blockstates.add(id(block.id),sourceState);
            blocks.add(id(block.id),p); return true;
        } catch(Unsupported | RuntimeException unsupported) { exclusions.addProperty(block.id,"model: "+unsupported.getMessage()); skipped++; return false; }
    }
    public Path finish() throws IOException {
        JsonObject manifest=new JsonObject(); manifest.addProperty("format","uebridge-block-textures"); manifest.addProperty("version",2);
        manifest.addProperty("skipped",skipped); manifest.add("blocks",blocks); manifest.add("textures",textures);
        manifest.add("blockstates",blockstates); manifest.add("models",exportedModels); manifest.add("excluded",exclusions);
        Path output=directory.resolve("manifest.json"); Files.writeString(output,new GsonBuilder().setPrettyPrinting().create().toJson(manifest),StandardOpenOption.CREATE_NEW); return output;
    }
    public int exported() { return blocks.size(); }
    public int skipped() { return skipped; }
}

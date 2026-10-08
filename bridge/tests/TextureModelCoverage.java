import dev.tsunami.bridge.TextureExport;
import com.google.gson.*;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;

/** Real local Minecraft assets exercise production parent/texture/model resolution. */
public final class TextureModelCoverage {
    private static void condition(JsonElement value,Map<String,String> props) {
        if(value==null || !value.isJsonObject()) return;
        for(var e:value.getAsJsonObject().entrySet()) {
            if(e.getValue().isJsonArray()) for(var term:e.getValue().getAsJsonArray()) condition(term,props);
            else if(e.getValue().isJsonPrimitive()) props.putIfAbsent(e.getKey(),e.getValue().getAsString().split("\\|",2)[0]);
        }
    }
    public static void main(String[] args) throws Exception {
        try(ZipFile jar=new ZipFile(args[0])) {
            TextureExport exporter=new TextureExport(id->{
                String[] pair=id.split(":",2);var entry=jar.getEntry("assets/"+pair[0]+"/"+pair[1]);
                if(entry==null) return null;try(var in=jar.getInputStream(entry)) {return in.readAllBytes();}
            },Path.of(args[1]));
            var entries=jar.stream().filter(e->e.getName().startsWith("assets/minecraft/blockstates/") && e.getName().endsWith(".json")).sorted(Comparator.comparing(ZipEntry::getName)).toList();
            for(var entry:entries) {
                String id="minecraft:"+entry.getName().substring("assets/minecraft/blockstates/".length(),entry.getName().length()-5);
                JsonObject state;try(var in=jar.getInputStream(entry)) {state=JsonParser.parseString(new String(in.readAllBytes(),java.nio.charset.StandardCharsets.UTF_8)).getAsJsonObject();}
                Map<String,String> props=new TreeMap<>();
                if(state.has("variants")) {
                    String selector=state.getAsJsonObject("variants").keySet().iterator().next();
                    if(!selector.isEmpty()) for(String part:selector.split(",")) {String[] p=part.split("=",2);props.put(p[0],p[1].split("\\|",2)[0]);}
                }
                if(state.has("multipart")) for(var part:state.getAsJsonArray("multipart")) condition(part.getAsJsonObject().get("when"),props);
                var box=List.of(new double[]{0,0,0,1,1,1});
                exporter.export(new TextureExport.Block(id,props,0xffffff,List.of(new TextureExport.State(props,box,box)),""));
            }
            Path manifest=exporter.finish();JsonObject report=JsonParser.parseString(Files.readString(manifest)).getAsJsonObject();
            JsonObject summary=new JsonObject();summary.addProperty("blockstates",entries.size());summary.addProperty("exported",exporter.exported());
            summary.addProperty("models",report.getAsJsonObject("models").size());summary.addProperty("textures",report.getAsJsonObject("textures").size());
            long itemFallbacks=report.getAsJsonObject("blocks").entrySet().stream().filter(e->e.getValue().getAsJsonObject().has("itemFallback") && e.getValue().getAsJsonObject().get("itemFallback").getAsBoolean()).count();
            summary.addProperty("dedicatedItemFallbacks",itemFallbacks);
            for(var entry:report.getAsJsonObject("excluded").entrySet()) if(!entry.getValue().getAsString().equals("model: Model has no renderable face"))
                throw new IllegalStateException("Unexpected ordinary model failure: "+entry.getKey()+": "+entry.getValue());
            summary.addProperty("excludedDedicatedOrInvisible",report.getAsJsonObject("excluded").size());
            System.out.println(new GsonBuilder().setPrettyPrinting().create().toJson(summary));
        }
    }
}

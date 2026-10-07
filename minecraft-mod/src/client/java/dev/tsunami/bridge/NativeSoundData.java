package dev.tsunami.bridge;

import com.google.gson.*;
import java.io.IOException;
import java.util.*;

/** Portable active-pack sound-event resolution; sample decoding remains in NativeSoundExport. */
public final class NativeSoundData {
    @FunctionalInterface public interface Samples {JsonObject wave(String id) throws IOException;}
    private NativeSoundData() {}
    public static JsonArray resolve(Map<String,JsonObject> definitions,String id,Samples samples) throws IOException {
        return resolve(definitions,TextureExport.id(id),samples,new HashSet<>(),0);
    }
    private static JsonArray resolve(Map<String,JsonObject> definitions,String id,Samples samples,Set<String> chain,int depth) throws IOException {
        if(depth>16 || !chain.add(id)) throw new IOException("Cyclic/deep sound reference: "+id);
        JsonObject definition=definitions.get(id);if(definition==null || !definition.has("sounds")) throw new IOException("Sound event missing: "+id);
        JsonArray result=new JsonArray();String namespace=id.substring(0,id.indexOf(':'));
        for(JsonElement e:definition.getAsJsonArray("sounds")) {
            JsonObject spec;if(e.isJsonPrimitive()) {spec=new JsonObject();spec.addProperty("name",e.getAsString());}else spec=e.getAsJsonObject();
            String name=spec.get("name").getAsString();name=TextureExport.id(name.contains(":")?name:namespace+":"+name);
            float volume=spec.has("volume")?spec.get("volume").getAsFloat():1,pitch=spec.has("pitch")?spec.get("pitch").getAsFloat():1;
            int weight=spec.has("weight")?spec.get("weight").getAsInt():1;
            if(!Float.isFinite(volume) || volume<0 || volume>16 || !Float.isFinite(pitch) || pitch<.1f || pitch>4 || weight<1 || weight>1024)
                throw new IOException("Invalid native sound variation");
            String type=spec.has("type")?spec.get("type").getAsString():"file";
            if(type.equals("event")) {
                JsonArray nested=resolve(definitions,name,samples,new HashSet<>(chain),depth+1);
                for(var child:nested) {
                    JsonObject variant=child.getAsJsonObject().deepCopy();
                    variant.addProperty("volume",Math.min(16,variant.get("volume").getAsFloat()*volume));
                    variant.addProperty("pitch",Math.max(.1f,Math.min(4,variant.get("pitch").getAsFloat()*pitch)));
                    // Minecraft's event-reference container reports the referenced
                    // event's total weight; its own spec weight does not affect selection.
                    result.add(variant);
                }
            } else if(type.equals("file")) {
                JsonObject sample=samples.wave(name);if(sample==null) throw new IOException("Sound sample missing: "+name);
                JsonObject variant=sample.deepCopy();variant.addProperty("volume",volume);variant.addProperty("pitch",pitch);variant.addProperty("weight",weight);result.add(variant);
            } else throw new IOException("Unsupported sound registration type: "+type);
            if(result.size()>1024) throw new IOException("Sound variation limit exceeded");
        }
        return result;
    }
}

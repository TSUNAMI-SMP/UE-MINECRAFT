package dev.tsunami.bridge;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import java.util.LinkedHashMap;
import java.util.Map;

/** A source colour at every cell voxel, including empty positions where UE may place a block.
 * Order is x + z*8 + y*64. RGB is the result of native biome blending, not a biome ID guess. */
public final class BiomeTintSnapshot {
    private BiomeTintSnapshot() {}
    public static JsonObject encode(int[][] colors) {
        if(colors==null || colors.length!=512) throw new IllegalArgumentException("Invalid biome tint cell");
        JsonArray palette=new JsonArray(),indices=new JsonArray();Map<String,Integer> seen=new LinkedHashMap<>();
        for(int[] color:colors) {
            if(color==null || color.length!=3) throw new IllegalArgumentException("Invalid biome tint triple");
            for(int value:color) if(value<0 || value>0xffffff) throw new IllegalArgumentException("Invalid biome tint color");
            String key=color[0]+":"+color[1]+":"+color[2];Integer index=seen.get(key);
            if(index==null) {index=palette.size();seen.put(key,index);palette.add(NativeExportData.array(color));}
            indices.add(index);
        }
        JsonObject result=new JsonObject();result.add("palette",palette);result.add("indices",indices);return result;
    }
}

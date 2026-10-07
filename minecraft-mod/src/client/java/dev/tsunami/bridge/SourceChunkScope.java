package dev.tsunami.bridge;

import java.util.HashSet;
import java.util.Set;

/** Pure chunk coordinates for a scope expressed in 8-block cells. */
final class SourceChunkScope {
    static Set<Long> chunks(int cellX,int cellZ,int radius) {
        if(radius<1 || radius>12 || Math.abs((long)cellX)>3_750_000 || Math.abs((long)cellZ)>3_750_000) throw new IllegalArgumentException("Invalid source cell scope");
        int minX=Math.floorDiv(cellX-radius,2),maxX=Math.floorDiv(cellX+radius,2),minZ=Math.floorDiv(cellZ-radius,2),maxZ=Math.floorDiv(cellZ+radius,2);
        Set<Long> chunks=new HashSet<>();
        for(int x=minX;x<=maxX;x++) for(int z=minZ;z<=maxZ;z++) chunks.add(pack(x,z));
        return Set.copyOf(chunks);
    }
    static long pack(int x,int z) {return (x&0xffffffffL)|((z&0xffffffffL)<<32);}
    static int x(long packed) {return (int)packed;}
    static int z(long packed) {return (int)(packed>>32);}
}

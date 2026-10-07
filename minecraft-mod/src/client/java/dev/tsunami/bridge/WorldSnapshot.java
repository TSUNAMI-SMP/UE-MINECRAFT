package dev.tsunami.bridge;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.nio.ByteBuffer;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/** Independent world protocol: one atomic, replaceable 8x8x8 cell at a time. */
public final class WorldSnapshot {
    public static final int CELL_SIZE = 8, ROWS_PER_BATCH = 8, MAX_SHAPES = 8192;
    public record Cell(int x, int y, int z) {
        public static Cell at(int x, int y, int z) {
            return new Cell(Math.floorDiv(x, CELL_SIZE), Math.floorDiv(y, CELL_SIZE), Math.floorDiv(z, CELL_SIZE));
        }
        public boolean inside(Cell center, int radius, int height) {
            return Math.abs((long)x - center.x) <= radius && Math.abs((long)y - center.y) <= height
                    && Math.abs((long)z - center.z) <= radius;
        }
    }
    public record Shape(double x, double y, double z, int color, double sx, double sy, double sz, String blockId, boolean collision, int blockX,int blockY,int blockZ,String stateKey,int role,int skyLight,int blockLight,int opacity,int emission) {
        public Shape(double x,double y,double z,int color,double sx,double sy,double sz,String blockId,boolean collision,int blockX,int blockY,int blockZ,String stateKey,int role) { this(x,y,z,color,sx,sy,sz,blockId,collision,blockX,blockY,blockZ,stateKey,role,15,0,15,0); }
        public Shape(double x,double y,double z,int color,double sx,double sy,double sz,String blockId,boolean collision,int blockX,int blockY,int blockZ) { this(x,y,z,color,sx,sy,sz,blockId,collision,blockX,blockY,blockZ,"",0); }
        public Shape(double x,double y,double z,int color,double sx,double sy,double sz,String blockId,boolean collision) { this(x,y,z,color,sx,sy,sz,blockId,collision,0,0,0); }
        public Shape(double x,double y,double z,int color,double sx,double sy,double sz) { this(x,y,z,color,sx,sy,sz,"",false); }
        public Shape(double x,double y,double z,int color,double sx,double sy,double sz,String blockId) { this(x,y,z,color,sx,sy,sz,blockId,false); }
        public Shape {
            if (!Double.isFinite(x) || !Double.isFinite(y) || !Double.isFinite(z)
                    || Math.abs(x) > 100000 || Math.abs(y) > 100000 || Math.abs(z) > 100000
                    || color < 0 || color > 0xffffff || !Double.isFinite(sx) || !Double.isFinite(sy) || !Double.isFinite(sz)
                    || sx <= 0 || sy <= 0 || sz <= 0 || sx > 4 || sy > 4 || sz > 4
                    || Math.abs((long)blockX)>30000000 || Math.abs((long)blockY)>30000000 || Math.abs((long)blockZ)>30000000
                    || blockId==null || blockId.length()>128 || (!blockId.isEmpty() && !blockId.matches("[a-z0-9_.-]+:[a-z0-9_./-]+"))
                    || stateKey==null || stateKey.length()>1024 || !stateKey.matches("[a-z0-9_=,.-]*") || role<0 || role>3
                    || skyLight<0 || skyLight>15 || blockLight<0 || blockLight>15 || opacity<0 || opacity>15 || emission<0 || emission>15)
                throw new IllegalArgumentException("Invalid world shape");
        }
    }
    public static void cellFields(JsonObject p, Cell cell) {
        p.addProperty("cellX", cell.x); p.addProperty("cellY", cell.y); p.addProperty("cellZ", cell.z);
    }
    public static String fingerprint(List<Shape> shapes) {
        try {
            MessageDigest hash=MessageDigest.getInstance("SHA-256"); ByteBuffer bytes=ByteBuffer.allocate(64);
            for (Shape s:shapes) {
                bytes.clear(); bytes.putDouble(s.x).putDouble(s.y).putDouble(s.z).putInt(s.color)
                    .putDouble(s.sx).putDouble(s.sy).putDouble(s.sz).putInt(s.blockX).putInt(s.blockY).putInt(s.blockZ); hash.update(bytes.array());
                byte[] id=s.blockId.getBytes(java.nio.charset.StandardCharsets.UTF_8);
                hash.update((byte)(s.collision ? 1 : 0)); hash.update((byte)id.length); hash.update(id);
                hash.update((byte)s.role); byte[] state=s.stateKey.getBytes(java.nio.charset.StandardCharsets.UTF_8);
                hash.update((byte)(state.length>>8)); hash.update((byte)state.length); hash.update(state);
                hash.update((byte)s.skyLight);hash.update((byte)s.blockLight);hash.update((byte)s.opacity);hash.update((byte)s.emission);
            }
            return java.util.HexFormat.of().formatHex(hash.digest());
        } catch (NoSuchAlgorithmException impossible) { throw new IllegalStateException(impossible); }
    }
    /** Dictionary-coded logical native blocks, including buried voxels for UE edits/light. */
    public static List<JsonObject> encodeCompact(Cell cell,List<Shape> shapes,double originX,double originY,double originZ) {
        if(shapes.size()>512 || !Double.isFinite(originX) || !Double.isFinite(originY) || !Double.isFinite(originZ)
                || Math.abs(originX)>30000000 || Math.abs(originY)>30000000 || Math.abs(originZ)>30000000)
            throw new IllegalArgumentException("Invalid compact cell/origin");
        String id=UUID.randomUUID().toString(); List<JsonObject> result=new ArrayList<>();
        JsonArray palette=new JsonArray(),rows=new JsonArray(); Map<String,Integer> indices=new java.util.LinkedHashMap<>();
        var owners=new java.util.HashSet<Integer>();
        for(Shape shape:shapes) {
            int x=shape.blockX-cell.x*8,y=shape.blockY-cell.y*8,z=shape.blockZ-cell.z*8;
            if(shape.role!=1 || shape.blockId.isEmpty() || x<0 || x>7 || y<0 || y>7 || z<0 || z>7 || !owners.add(x+(z<<3)+(y<<6)))
                throw new IllegalArgumentException("Compact cells require one native visual per local voxel");
            JsonArray entry=new JsonArray();entry.add(shape.blockId);entry.add(shape.stateKey);entry.add(shape.color);entry.add(shape.opacity);entry.add(shape.emission);
            String key=entry.toString(); Integer p=indices.get(key);
            JsonArray row=new JsonArray();row.add(x+(z<<3)+(y<<6));row.add(p==null ? palette.size() : p);row.add(shape.skyLight);row.add(shape.blockLight);
            int candidateBytes=palette.toString().length()+rows.toString().length()+row.toString().length()+(p==null ? key.length() : 0);
            if(rows.size()>0 && (rows.size()>=128 || (p==null && palette.size()>=32) || candidateBytes>1396)) {
                result.add(compactPacket(cell,id,originX,originY,originZ,palette,rows));palette=new JsonArray();rows=new JsonArray();indices.clear();p=null;
                row.set(1,new com.google.gson.JsonPrimitive(0));
            }
            if(p==null) {p=palette.size();indices.put(key,p);palette.add(entry);}
            rows.add(row);
            if(palette.toString().length()+rows.toString().length()>1400) throw new IllegalArgumentException("Compact native state exceeds UDP budget");
        }
        if(rows.size()>0 || result.isEmpty()) result.add(compactPacket(cell,id,originX,originY,originZ,palette,rows));
        for(int i=0;i<result.size();i++) {result.get(i).addProperty("batchIndex",i);result.get(i).addProperty("totalBatches",result.size());}
        return result;
    }
    private static JsonObject compactPacket(Cell cell,String id,double x,double y,double z,JsonArray palette,JsonArray rows) {
        JsonObject packet=new JsonObject();packet.addProperty("event","world_cell_compact");cellFields(packet,cell);
        packet.addProperty("snapshotId",id);packet.addProperty("ox",x);packet.addProperty("oy",y);packet.addProperty("oz",z);
        packet.add("palette",palette);packet.add("blocks",rows);return packet;
    }
    public static List<JsonObject> encode(Cell cell, List<Shape> shapes) {
        return encode(cell,shapes,false);
    }
    public static List<JsonObject> encode(Cell cell,List<Shape> shapes,boolean textured) {
        return encode(cell,shapes,textured,false);
    }
    public static List<JsonObject> encode(Cell cell,List<Shape> shapes,boolean textured,boolean physics) {
        return encode(cell,shapes,textured,physics,false);
    }
    public static List<JsonObject> encode(Cell cell,List<Shape> shapes,boolean textured,boolean physics,boolean owners) {
        textured=textured || physics;
        if (shapes.size() > MAX_SHAPES) throw new IllegalArgumentException("World cell shape limit exceeded");
        if(textured && shapes.stream().anyMatch(s->s.blockId.isEmpty())) throw new IllegalArgumentException("Textured rows need block IDs");
        String id = UUID.randomUUID().toString();
        int perBatch=textured ? 4 : ROWS_PER_BATCH;
        List<JsonArray> batches=new ArrayList<>(); JsonArray batch=new JsonArray(); int bytes=0;
        // Leave room for the session/sequence/ACK envelope. A long native state must
        // never turn an otherwise valid initial import into an oversized UDP packet.
        for(Shape s:shapes) {
            JsonArray row=new JsonArray(); row.add(s.x); row.add(s.y); row.add(s.z); row.add(s.color);
            row.add(s.sx); row.add(s.sy); row.add(s.sz); if(textured) row.add(s.blockId); if(physics) row.add(s.collision);
            if(physics && owners) {
                row.add(s.blockX);row.add(s.blockY);row.add(s.blockZ);
                if(s.role!=0) { row.add(s.stateKey);row.add(s.role); }
            }
            int length=row.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8).length+1;
            if(length>1500) throw new IllegalArgumentException("World row exceeds UDP budget");
            if(batch.size()>0 && (batch.size()>=perBatch || bytes+length>1500)) { batches.add(batch);batch=new JsonArray();bytes=0; }
            batch.add(row);bytes+=length;
        }
        if(batch.size()>0 || batches.isEmpty()) batches.add(batch);
        int total=batches.size();
        List<JsonObject> packets = new ArrayList<>(total);
        for (int i = 0; i < total; i++) {
            JsonObject p = new JsonObject(); p.addProperty("event", physics ? "world_cell_physics" : (textured ? "world_cell_textured" : "world_cell")); cellFields(p, cell);
            p.addProperty("snapshotId", id); p.addProperty("batchIndex", i); p.addProperty("totalBatches", total);
            p.add("blocks", batches.get(i)); packets.add(p);
        }
        return packets;
    }
}

package dev.tsunami.bridge;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import java.util.ArrayList;
import java.util.List;
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
    public record Shape(double x, double y, double z, int color, double sx, double sy, double sz, String blockId, boolean collision, int blockX,int blockY,int blockZ) {
        public Shape(double x,double y,double z,int color,double sx,double sy,double sz,String blockId,boolean collision) { this(x,y,z,color,sx,sy,sz,blockId,collision,0,0,0); }
        public Shape(double x,double y,double z,int color,double sx,double sy,double sz) { this(x,y,z,color,sx,sy,sz,"",false); }
        public Shape(double x,double y,double z,int color,double sx,double sy,double sz,String blockId) { this(x,y,z,color,sx,sy,sz,blockId,false); }
        public Shape {
            if (!Double.isFinite(x) || !Double.isFinite(y) || !Double.isFinite(z)
                    || Math.abs(x) > 100000 || Math.abs(y) > 100000 || Math.abs(z) > 100000
                    || color < 0 || color > 0xffffff || !Double.isFinite(sx) || !Double.isFinite(sy) || !Double.isFinite(sz)
                    || sx <= 0 || sy <= 0 || sz <= 0 || sx > 4 || sy > 4 || sz > 4
                    || Math.abs((long)blockX)>30000000 || Math.abs((long)blockY)>30000000 || Math.abs((long)blockZ)>30000000
                    || blockId==null || blockId.length()>128 || (!blockId.isEmpty() && !blockId.matches("[a-z0-9_.-]+:[a-z0-9_./-]+")))
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
            }
            return java.util.HexFormat.of().formatHex(hash.digest());
        } catch (NoSuchAlgorithmException impossible) { throw new IllegalStateException(impossible); }
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
        int total = Math.max(1, (shapes.size() + perBatch - 1) / perBatch);
        List<JsonObject> packets = new ArrayList<>(total);
        for (int i = 0; i < total; i++) {
            JsonObject p = new JsonObject(); p.addProperty("event", physics ? "world_cell_physics" : (textured ? "world_cell_textured" : "world_cell")); cellFields(p, cell);
            p.addProperty("snapshotId", id); p.addProperty("batchIndex", i); p.addProperty("totalBatches", total);
            JsonArray rows = new JsonArray();
            for (int n = i * perBatch; n < Math.min(shapes.size(), (i + 1) * perBatch); n++) {
                Shape s = shapes.get(n); JsonArray row = new JsonArray();
                row.add(s.x); row.add(s.y); row.add(s.z); row.add(s.color);
                row.add(s.sx); row.add(s.sy); row.add(s.sz); if(textured) row.add(s.blockId); if(physics) row.add(s.collision);
                if(physics && owners) { row.add(s.blockX);row.add(s.blockY);row.add(s.blockZ); } rows.add(row);
            }
            p.add("blocks", rows); packets.add(p);
        }
        return packets;
    }
}

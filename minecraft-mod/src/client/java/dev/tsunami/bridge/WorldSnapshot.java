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
    public record Shape(double x, double y, double z, int color, double sx, double sy, double sz) {
        public Shape {
            if (!Double.isFinite(x) || !Double.isFinite(y) || !Double.isFinite(z)
                    || Math.abs(x) > 100000 || Math.abs(y) > 100000 || Math.abs(z) > 100000
                    || color < 0 || color > 0xffffff || !Double.isFinite(sx) || !Double.isFinite(sy) || !Double.isFinite(sz)
                    || sx <= 0 || sy <= 0 || sz <= 0 || sx > 4 || sy > 4 || sz > 4)
                throw new IllegalArgumentException("Invalid world shape");
        }
    }
    public static void cellFields(JsonObject p, Cell cell) {
        p.addProperty("cellX", cell.x); p.addProperty("cellY", cell.y); p.addProperty("cellZ", cell.z);
    }
    public static String fingerprint(List<Shape> shapes) {
        try {
            MessageDigest hash=MessageDigest.getInstance("SHA-256"); ByteBuffer bytes=ByteBuffer.allocate(52);
            for (Shape s:shapes) {
                bytes.clear(); bytes.putDouble(s.x).putDouble(s.y).putDouble(s.z).putInt(s.color)
                    .putDouble(s.sx).putDouble(s.sy).putDouble(s.sz); hash.update(bytes.array());
            }
            return java.util.HexFormat.of().formatHex(hash.digest());
        } catch (NoSuchAlgorithmException impossible) { throw new IllegalStateException(impossible); }
    }
    public static List<JsonObject> encode(Cell cell, List<Shape> shapes) {
        if (shapes.size() > MAX_SHAPES) throw new IllegalArgumentException("World cell shape limit exceeded");
        String id = UUID.randomUUID().toString();
        int total = Math.max(1, (shapes.size() + ROWS_PER_BATCH - 1) / ROWS_PER_BATCH);
        List<JsonObject> packets = new ArrayList<>(total);
        for (int i = 0; i < total; i++) {
            JsonObject p = new JsonObject(); p.addProperty("event", "world_cell"); cellFields(p, cell);
            p.addProperty("snapshotId", id); p.addProperty("batchIndex", i); p.addProperty("totalBatches", total);
            JsonArray rows = new JsonArray();
            for (int n = i * ROWS_PER_BATCH; n < Math.min(shapes.size(), (i + 1) * ROWS_PER_BATCH); n++) {
                Shape s = shapes.get(n); JsonArray row = new JsonArray();
                row.add(s.x); row.add(s.y); row.add(s.z); row.add(s.color);
                row.add(s.sx); row.add(s.sy); row.add(s.sz); rows.add(row);
            }
            p.add("blocks", rows); packets.add(p);
        }
        return packets;
    }
}

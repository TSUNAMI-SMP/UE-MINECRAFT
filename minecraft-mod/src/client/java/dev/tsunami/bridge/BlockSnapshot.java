package dev.tsunami.bridge;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

/** Portable bounded snapshot encoder. No game classes and no unbounded datagrams. */
public final class BlockSnapshot {
    public static final int BLOCKS_PER_BATCH = 12;
    public static final int MAX_BLOCKS = 2601;
    public record Block(double x, double y, double z, int color) {
        public Block {
            if (!Double.isFinite(x) || !Double.isFinite(y) || !Double.isFinite(z)
                    || Math.abs(x) > 100000 || Math.abs(y) > 100000 || Math.abs(z) > 100000
                    || color < 0 || color > 0xffffff) throw new IllegalArgumentException("Invalid preview block");
        }
    }
    public static List<JsonObject> encode(List<Block> blocks) {
        if (blocks.size() > MAX_BLOCKS) throw new IllegalArgumentException("Preview block limit exceeded");
        String id = UUID.randomUUID().toString();
        int total = Math.max(1, (blocks.size() + BLOCKS_PER_BATCH - 1) / BLOCKS_PER_BATCH);
        List<JsonObject> result = new ArrayList<>(total);
        for (int i = 0; i < total; i++) {
            JsonObject p = new JsonObject(); p.addProperty("event", "block_snapshot");
            p.addProperty("snapshotId", id); p.addProperty("batchIndex", i); p.addProperty("totalBatches", total);
            JsonArray data = new JsonArray();
            for (int j = i * BLOCKS_PER_BATCH; j < Math.min(blocks.size(), (i + 1) * BLOCKS_PER_BATCH); j++) {
                Block b = blocks.get(j); JsonArray row = new JsonArray();
                row.add(b.x); row.add(b.y); row.add(b.z); row.add(b.color); data.add(row);
            }
            p.add("blocks", data); result.add(p);
        }
        return result;
    }
}

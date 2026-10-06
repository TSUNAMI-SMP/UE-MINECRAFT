package dev.tsunami.bridge;
import com.google.gson.JsonObject;
import org.junit.Test;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import static org.junit.Assert.*;

public class BlockSnapshotTest {
    @Test public void largestSnapshotFitsPacketsAndPreservesAllBlocks() {
        List<BlockSnapshot.Block> blocks = new ArrayList<>();
        for (int i = 0; i < BlockSnapshot.MAX_BLOCKS; i++) blocks.add(new BlockSnapshot.Block(99999.123456789, -99999.123456789, i, 0xffffff));
        var packets = BlockSnapshot.encode(blocks); assertEquals(217, packets.size());
        String id = packets.getFirst().get("snapshotId").getAsString(); int count = 0;
        for (int i = 0; i < packets.size(); i++) {
            JsonObject p = packets.get(i); assertEquals(i, p.get("batchIndex").getAsInt()); assertEquals(id, p.get("snapshotId").getAsString());
            count += p.getAsJsonArray("blocks").size();
            p.addProperty("v", 1); p.addProperty("kind", "event"); p.addProperty("seq", 9007199254740991L);
            p.addProperty("session", id); p.addProperty("eventId", id); p.addProperty("snapshotSeq", 9007199254740991L);
            p.addProperty("x", 0); p.addProperty("y", 0); p.addProperty("z", 0);
            assertTrue("Datagram exceeds cap", p.toString().getBytes(StandardCharsets.UTF_8).length <= BridgeTransport.MAX_PACKET_BYTES);
        }
        assertEquals(BlockSnapshot.MAX_BLOCKS, count);
    }
    @Test public void emptySnapshotIsOneClearReplacementBatch() {
        var packets = BlockSnapshot.encode(List.of()); assertEquals(1, packets.size()); assertEquals(0, packets.getFirst().getAsJsonArray("blocks").size());
    }
    @Test public void unsupportedSizesAndCoordinatesRejected() {
        try { new BlockSnapshot.Block(Double.NaN, 0, 0, 0); fail(); } catch (IllegalArgumentException expected) { }
        try { new BlockSnapshot.Block(0, 0, 0, -1); fail(); } catch (IllegalArgumentException expected) { }
        var tooMany = java.util.Collections.nCopies(BlockSnapshot.MAX_BLOCKS + 1, new BlockSnapshot.Block(0, 0, 0, 0));
        try { BlockSnapshot.encode(tooMany); fail(); } catch (IllegalArgumentException expected) { }
    }
}

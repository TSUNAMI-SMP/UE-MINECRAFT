package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import java.nio.charset.StandardCharsets;
import java.util.*;
import org.junit.Test;
import static org.junit.Assert.*;

public class WorldSnapshotTest {
    @Test public void negativeCoordinatesAndCellBounds() {
        assertEquals(new WorldSnapshot.Cell(-1,-2,1),WorldSnapshot.Cell.at(-1,-9,15));
        assertTrue(new WorldSnapshot.Cell(-3,1,2).inside(new WorldSnapshot.Cell(-1,0,0),2,1));
        assertFalse(new WorldSnapshot.Cell(-3,2,2).inside(new WorldSnapshot.Cell(-1,0,0),2,1));
    }
    @Test public void allShapesRoundTripThroughBoundedBatches() {
        List<WorldSnapshot.Shape> shapes=new ArrayList<>();
        for (int i=0;i<512;i++) shapes.add(new WorldSnapshot.Shape(-99999.123456789,123.456789,-99999.987654321,0xffffff,0.123456789,1,2));
        var packets=WorldSnapshot.encode(new WorldSnapshot.Cell(-3750000,-8,3750000),shapes);
        assertEquals(64,packets.size()); int count=0; String snapshotId=packets.getFirst().get("snapshotId").getAsString();
        for (int i=0;i<packets.size();i++) {
            JsonObject p=packets.get(i); assertEquals(i,p.get("batchIndex").getAsInt()); assertEquals(snapshotId,p.get("snapshotId").getAsString());
            for (var value:p.getAsJsonArray("blocks")) { var row=value.getAsJsonArray(); assertEquals(7,row.size()); assertEquals(shapes.get(count++).sx(),row.get(4).getAsDouble(),0); }
            p.addProperty("v",1); p.addProperty("kind","event"); p.addProperty("session",UUID.randomUUID().toString());
            p.addProperty("eventId",UUID.randomUUID().toString()); p.addProperty("seq",9007199254740991L);
            p.addProperty("snapshotSeq",9007199254740000L); p.addProperty("x",0); p.addProperty("y",0); p.addProperty("z",0);
            assertTrue(p.toString().getBytes(StandardCharsets.UTF_8).length<=BridgeTransport.MAX_PACKET_BYTES);
        }
        assertEquals(shapes.size(),count);
    }
    @Test public void emptyCellExplicitlyClearsOldGeometry() {
        var packets=WorldSnapshot.encode(new WorldSnapshot.Cell(0,0,0),List.of());
        assertEquals(1,packets.size()); assertEquals(0,packets.getFirst().getAsJsonArray("blocks").size());
    }
    @Test public void fingerprintDetectsColorAndShapeChangesWithoutRetainingGeometry() {
        var a=new WorldSnapshot.Shape(0,0,0,0xff0000,1,1,1);
        var b=new WorldSnapshot.Shape(0,0,0,0x00ff00,1,1,1);
        var c=new WorldSnapshot.Shape(0,0,0,0xff0000,1,.5,1);
        assertEquals(WorldSnapshot.fingerprint(List.of(a)),WorldSnapshot.fingerprint(List.of(a)));
        assertNotEquals(WorldSnapshot.fingerprint(List.of(a)),WorldSnapshot.fingerprint(List.of(b)));
        assertNotEquals(WorldSnapshot.fingerprint(List.of(a)),WorldSnapshot.fingerprint(List.of(c)));
        assertNotEquals(WorldSnapshot.fingerprint(List.of(a)),WorldSnapshot.fingerprint(List.of()));
    }
    @Test public void rejectMalformedOrUnboundedShapes() {
        for (double size:new double[]{0,-1,Double.NaN,Double.POSITIVE_INFINITY,5}) {
            try { new WorldSnapshot.Shape(0,0,0,0,size,1,1); fail(); } catch (IllegalArgumentException expected) { }
        }
        try { WorldSnapshot.encode(new WorldSnapshot.Cell(0,0,0),Collections.nCopies(8193,new WorldSnapshot.Shape(0,0,0,0,1,1,1))); fail(); }
        catch (IllegalArgumentException expected) { }
    }
    @Test public void texturedRowsRemainBoundedAndCarryBlockIdentity() {
        String id="minecraft:"+"a".repeat(118);
        var shape=new WorldSnapshot.Shape(99999.123456789,-99999.123456789,99999.123456789,0xffffff,.123456789,.123456789,.123456789,id);
        var packets=WorldSnapshot.encode(new WorldSnapshot.Cell(-3750000,3750000,-3750000),Collections.nCopies(8,shape),true);
        assertEquals(2,packets.size());
        for(var p:packets) {
            assertEquals("world_cell_textured",p.get("event").getAsString());
            assertEquals(id,p.getAsJsonArray("blocks").get(0).getAsJsonArray().get(7).getAsString());
            p.addProperty("v",1); p.addProperty("kind","event"); p.addProperty("session",UUID.randomUUID().toString());
            p.addProperty("eventId",UUID.randomUUID().toString()); p.addProperty("seq",9007199254740991L);
            p.addProperty("snapshotSeq",9007199254740000L); p.addProperty("x",0); p.addProperty("y",0); p.addProperty("z",0);
            assertTrue(p.toString().getBytes(StandardCharsets.UTF_8).length<=BridgeTransport.MAX_PACKET_BYTES);
        }
    }
    @Test public void sameColorDifferentBlockTriggersNewFingerprint() {
        var a=new WorldSnapshot.Shape(0,0,0,0xaaaaaa,1,1,1,"minecraft:stone");
        var b=new WorldSnapshot.Shape(0,0,0,0xaaaaaa,1,1,1,"minecraft:cobblestone");
        assertNotEquals(WorldSnapshot.fingerprint(List.of(a)),WorldSnapshot.fingerprint(List.of(b)));
    }
}

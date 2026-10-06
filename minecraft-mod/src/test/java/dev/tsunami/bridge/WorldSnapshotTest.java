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

    @org.junit.Test public void physicsRowsCarryCollisionAndStayBounded() throws Exception {
        var solid=new WorldSnapshot.Shape(-99999.123456789,99999.123456789,99999.123456789,0xffffff,4,4,4,"minecraft:"+"a".repeat(118),true);
        var visual=new WorldSnapshot.Shape(solid.x(),solid.y(),solid.z(),solid.color(),4,4,4,solid.blockId(),false);
        assertNotEquals(WorldSnapshot.fingerprint(java.util.List.of(solid)),WorldSnapshot.fingerprint(java.util.List.of(visual)));
        var packets=WorldSnapshot.encode(new WorldSnapshot.Cell(4000000,-4000000,4000000),java.util.List.of(solid,solid,solid,visual),true,true);
        var p=packets.get(0); assertEquals("world_cell_physics",p.get("event").getAsString());
        assertEquals(9,p.getAsJsonArray("blocks").get(0).getAsJsonArray().size());
        assertTrue(p.getAsJsonArray("blocks").get(0).getAsJsonArray().get(8).getAsBoolean());
        assertFalse(p.getAsJsonArray("blocks").get(3).getAsJsonArray().get(8).getAsBoolean());
        p.addProperty("v",1);p.addProperty("kind","event");p.addProperty("session",java.util.UUID.randomUUID().toString());
        p.addProperty("eventId",java.util.UUID.randomUUID().toString());p.addProperty("snapshotSeq",9007199254740991L);
        p.addProperty("seq",9007199254740991L);p.addProperty("x",0);p.addProperty("y",0);p.addProperty("z",0);
        assertTrue(p.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8).length<=2048);
    }
    @Test public void physicsOwnersPreservePartialBlockIdentityWithinPacketBudget() {
        var shape=new WorldSnapshot.Shape(-99999.123456789,99999.123456789,99999.123456789,0xffffff,4,4,4,"minecraft:"+"a".repeat(118),true,-30000000,30000000,-30000000);
        var p=WorldSnapshot.encode(new WorldSnapshot.Cell(-3750000,3750000,-3750000),Collections.nCopies(4,shape),true,true,true).getFirst();
        var row=p.getAsJsonArray("blocks").get(0).getAsJsonArray();assertEquals(12,row.size());assertEquals(-30000000,row.get(9).getAsInt());
        p.addProperty("v",1);p.addProperty("kind","event");p.addProperty("session",UUID.randomUUID().toString());
        p.addProperty("eventId",UUID.randomUUID().toString());p.addProperty("snapshotSeq",9007199254740991L);p.addProperty("seq",9007199254740991L);
        p.addProperty("x",0);p.addProperty("y",0);p.addProperty("z",0);
        assertTrue(p.toString().getBytes(StandardCharsets.UTF_8).length<=2048);
    }
    @Test public void ownerIdentityChangesFingerprintAndRejectsImpossibleCoordinates() {
        var a=new WorldSnapshot.Shape(0,0,0,0,1,1,1,"minecraft:stone",true,0,0,0);
        var b=new WorldSnapshot.Shape(0,0,0,0,1,1,1,"minecraft:stone",true,1,0,0);
        assertNotEquals(WorldSnapshot.fingerprint(List.of(a)),WorldSnapshot.fingerprint(List.of(b)));
        try {new WorldSnapshot.Shape(0,0,0,0,1,1,1,"minecraft:stone",true,Integer.MIN_VALUE,0,0);fail();}
        catch(IllegalArgumentException expected) {}
    }
    @Test public void longStateKeysSplitIntoSafeModelBatchesAndFingerprintIncludesRole() {
        String state="a="+"a".repeat(1022);
        var shape=new WorldSnapshot.Shape(-99999.123456789,99999.123456789,99999.123456789,0xffffff,1,1,1,
            "minecraft:"+"a".repeat(118),false,-30000000,30000000,-30000000,state,1);
        var outline=new WorldSnapshot.Shape(shape.x(),shape.y(),shape.z(),shape.color(),1,1,1,shape.blockId(),false,
            shape.blockX(),shape.blockY(),shape.blockZ(),state,3);
        assertNotEquals(WorldSnapshot.fingerprint(List.of(shape)),WorldSnapshot.fingerprint(List.of(outline)));
        var packets=WorldSnapshot.encode(new WorldSnapshot.Cell(-3750000,3750000,-3750000),Collections.nCopies(8,shape),true,true,true);
        assertEquals(8,packets.size());
        for(var p:packets) {
            assertEquals(14,p.getAsJsonArray("blocks").get(0).getAsJsonArray().size());
            p.addProperty("v",1);p.addProperty("kind","event");p.addProperty("session",UUID.randomUUID().toString());
            p.addProperty("eventId",UUID.randomUUID().toString());p.addProperty("snapshotSeq",9007199254740991L);p.addProperty("seq",9007199254740991L);
            p.addProperty("x",0);p.addProperty("y",0);p.addProperty("z",0);
            assertTrue(p.toString().getBytes(StandardCharsets.UTF_8).length<=2048);
        }
    }
}

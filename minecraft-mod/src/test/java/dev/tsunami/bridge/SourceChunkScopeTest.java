package dev.tsunami.bridge;

import org.junit.Test;
import static org.junit.Assert.*;

public final class SourceChunkScopeTest {
    @Test public void fourToSixChunkRadiiCover81To169NativeChunks() {
        assertEquals(81,SourceChunkScope.chunks(0,0,8).size());
        assertEquals(169,SourceChunkScope.chunks(0,0,12).size());
        assertEquals(169,SourceChunkScope.chunks(-1,1,12).size());
    }
    @Test public void negativeHalfChunkCenterUsesFloorDivision() {
        var chunks=SourceChunkScope.chunks(-1,-1,1);
        assertEquals(4,chunks.size());assertTrue(chunks.contains(SourceChunkScope.pack(-1,-1)));assertTrue(chunks.contains(SourceChunkScope.pack(0,0)));
        assertFalse(chunks.contains(SourceChunkScope.pack(1,1)));
    }
    @Test public void packedCoordinatesRoundTripSignedValuesWithoutCollisions() {
        for(int x:new int[]{-1_875_000,-1,0,1,1_875_000}) for(int z:new int[]{-1_875_000,-1,0,1,1_875_000}) {
            long key=SourceChunkScope.pack(x,z);assertEquals(x,SourceChunkScope.x(key));assertEquals(z,SourceChunkScope.z(key));
        }
    }
    @Test public void scopeMovesWithoutKeepingUnboundedOldChunkTickets() {
        var old=SourceChunkScope.chunks(0,0,12);var moved=SourceChunkScope.chunks(26,0,12);
        assertTrue(java.util.Collections.disjoint(old,moved));assertEquals(169,moved.size());
    }
    @Test public void rejectsUnboundedScopes() {
        for(int radius:new int[]{0,13,Integer.MAX_VALUE}) try {SourceChunkScope.chunks(0,0,radius);fail("Unbounded radius accepted");}catch(IllegalArgumentException expected) { }
        try {SourceChunkScope.chunks(Integer.MAX_VALUE,0,12);fail("Overflowing scope accepted");}catch(IllegalArgumentException expected) { }
    }
}

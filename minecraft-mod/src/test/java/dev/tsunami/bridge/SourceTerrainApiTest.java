package dev.tsunami.bridge;

import org.junit.Test;
import static org.junit.Assert.*;

public final class SourceTerrainApiTest {
    @Test public void bindsTheNonblocking12111ChunkLoadingApi() throws Exception {
        ClassLoader loader=getClass().getClassLoader();
        Class<?> manager=Class.forName("net.minecraft.server.world.ServerChunkManager",false,loader);
        Class<?> ticket=Class.forName("net.minecraft.server.world.ChunkTicketType",false,loader);
        Class<?> pos=Class.forName("net.minecraft.util.math.ChunkPos",false,loader);
        assertEquals(java.util.concurrent.CompletableFuture.class,manager.getMethod("addChunkLoadingTicket",ticket,pos,int.class).getReturnType());
        assertNotNull(manager.getMethod("getWorldChunk",int.class,int.class));
        assertNotNull(manager.getMethod("removeTicket",ticket,pos,int.class));
        assertNotNull(ticket.getConstructor(long.class,int.class));
    }
    @Test public void nativeFarMobSnapshotsUseTheBoundedSpatialEntityApi() throws Exception {
        ClassLoader loader=getClass().getClassLoader();
        Class<?> world=Class.forName("net.minecraft.server.world.ServerWorld",false,loader);
        Class<?> filter=Class.forName("net.minecraft.util.TypeFilter",false,loader);
        Class<?> box=Class.forName("net.minecraft.util.math.Box",false,loader);
        assertNotNull(world.getMethod("collectEntitiesByType",filter,box,java.util.function.Predicate.class,java.util.List.class,int.class));
    }
}

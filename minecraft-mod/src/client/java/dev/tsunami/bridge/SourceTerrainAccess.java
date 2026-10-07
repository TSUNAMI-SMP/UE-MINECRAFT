package dev.tsunami.bridge;

import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;
import net.minecraft.block.BlockState;
import net.minecraft.block.Blocks;
import net.minecraft.block.entity.BlockEntity;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.color.block.BlockColors;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.Registries;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.server.world.ChunkTicketType;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.ChunkPos;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.LightType;
import net.minecraft.world.World;
import net.minecraft.world.BlockRenderView;
import net.minecraft.world.biome.ColorResolver;
import net.minecraft.world.chunk.light.LightingProvider;
import net.minecraft.fluid.FluidState;
import net.minecraft.util.math.Direction;
import net.minecraft.world.chunk.WorldChunk;

/** Read-only cell cache sourced from a dedicated integrated server, independently of the frozen player.
 * Loading-only, non-serialized, expiring tickets may generate chunks as ordinary local exploration does.
 * No join(), blocking getChunk(), player teleport, source block edit, or dedicated/LAN server access.
 */
public final class SourceTerrainAccess {
    static final int MAX_CHUNK_REQUESTS=8, NEW_CHUNKS_PER_JOB=2, MAX_CACHED_CELLS=256, MAX_CELL_REQUESTS=32;
    static final int MAX_BLOCK_READS=4096, MAX_CELLS_PER_JOB=8;
    static final long JOB_INTERVAL=50_000_000L, JOB_BUDGET=4_000_000L;
    private Session session;
    public void tick(MinecraftClient client,Vec3d absoluteCenter,int radius,int halfHeight) {
        if(client.world==null || client.player==null || client.getServer()==null || client.getServer().isRemote() || client.isPaused() || absoluteCenter==null
                || radius<1 || radius>12 || halfHeight<1 || halfHeight>6) {release();return;}
        var server=client.getServer();var dimension=client.world.getRegistryKey();
        if(session==null || session.server!=server || !session.dimension.equals(dimension)) {
            release();session=new Session(server,dimension,client.getBlockColors());
        }
        Session current=session;
        current.scope=new Scope(WorldSnapshot.Cell.at((int)Math.floor(absoluteCenter.x),(int)Math.floor(absoluteCenter.y),(int)Math.floor(absoluteCenter.z)),radius,halfHeight);
        long now=System.nanoTime();
        if(now-current.lastSchedule<JOB_INTERVAL || !current.scheduled.compareAndSet(false,true)) return;
        current.lastSchedule=now;server.execute(() -> {
            try {current.service();}
            catch(RuntimeException error) {
                current.reason="source cell failed: "+error.getClass().getSimpleName();
                if(current.capture!=null) current.requested.remove(current.capture.cell);current.capture=null;
            } finally {current.scheduled.set(false);}
        });
    }
    public void release() {
        Session previous=session;session=null;if(previous==null) return;
        previous.closed=true;previous.cache.clear();previous.requests.clear();previous.requested.clear();
        previous.server.execute(previous::cleanup);
    }
    public boolean hasCell(WorldSnapshot.Cell cell) {return session!=null && freshSnapshot(session,cell)!=null;}
    public void invalidate(WorldSnapshot.Cell cell) {
        Session current=session;if(current==null || cell==null || current.scope==null || !cell.inside(current.scope.center,current.scope.radius,current.scope.height)) return;
        current.revisions.merge(cell,1L,Long::sum);current.cache.remove(cell);
    }
    public List<WorldSnapshot.Shape> snapshotCell(WorldSnapshot.Cell cell,Vec3d origin) {
        Session current=session;if(current==null || current.closed || origin==null || current.scope==null || !cell.inside(current.scope.center,current.scope.radius,current.scope.height)) return null;
        Snapshot cached=freshSnapshot(current,cell);
        if(cached==null) {
            if(current.requested.size()<MAX_CELL_REQUESTS && current.requested.add(cell)) current.requests.add(cell);
            return null;
        }
        List<WorldSnapshot.Shape> result=new ArrayList<>(cached.voxels.size());
        for(Voxel voxel:cached.voxels) {
            double x=voxel.x+.5+voxel.offset.x-origin.x,y=voxel.y+.5+voxel.offset.y-origin.y,z=voxel.z+.5+voxel.offset.z-origin.z;
            if(Math.abs(x)>99995 || Math.abs(y)>99995 || Math.abs(z)>99995) continue;
            result.add(new WorldSnapshot.Shape(x,y,z,voxel.color,1,1,1,voxel.metadata.id,false,voxel.x,voxel.y,voxel.z,voxel.metadata.state,1,voxel.sky,voxel.light,voxel.metadata.opacity,voxel.metadata.emission));
        }
        return List.copyOf(result);
    }
    public int[] skyTop(WorldSnapshot.Cell cell) {
        Session current=session;Snapshot cached=current==null ? null : freshSnapshot(current,cell);return cached==null ? null : cached.top.clone();
    }
    private static Snapshot freshSnapshot(Session current,WorldSnapshot.Cell cell) {
        Snapshot cached=current.cache.get(cell);
        return cached!=null && cached.revision==current.revisions.getOrDefault(cell,0L) ? cached : null;
    }
    public String status() {
        Session current=session;
        return current==null ? "MC地形ソース停止" : "MC地形チャンク="+current.readyChunks+"/"+current.targetChunks+" 読込中="+current.loadingChunks+" セル待ち="+current.requested.size()+" キャッシュ="+current.cache.size()+(current.reason.isEmpty() ? "" : " / "+current.reason);
    }
    private record Scope(WorldSnapshot.Cell center,int radius,int height) { }
    private record Metadata(String id,String state,int opacity,int emission,boolean supported) { }
    private record Voxel(int x,int y,int z,Vec3d offset,int color,int sky,int light,Metadata metadata) { }
    private record Snapshot(List<Voxel> voxels,int[] top,long revision) { }
    private static final class Load {
        final ChunkPos pos;
        final CompletableFuture<?> loading;
        long refreshed;
        Load(ChunkPos pos,CompletableFuture<?> loading,long now) {this.pos=pos;this.loading=loading;this.refreshed=now;}
    }
    private static final class Capture {
        final WorldSnapshot.Cell cell;
        final long revision;
        final List<Voxel> voxels=new ArrayList<>();
        final int[] top=new int[64];
        int cursor;
        Capture(WorldSnapshot.Cell cell,long revision) {this.cell=cell;this.revision=revision;}
    }
    private static final class Session {
        // Initialized only in a running local game; pure import/protocol tests do not bootstrap Minecraft registries.
        // A distinct temporary loading-only type cannot remove another subsystem's native tickets.
        private static final ChunkTicketType TICKET=new ChunkTicketType(43,ChunkTicketType.FOR_LOADING);
        final IntegratedServer server;
        final RegistryKey<World> dimension;
        final BlockColors colors;
        final AtomicBoolean scheduled=new AtomicBoolean();
        final ConcurrentHashMap<WorldSnapshot.Cell,Snapshot> cache=new ConcurrentHashMap<>();
        final Set<WorldSnapshot.Cell> requested=ConcurrentHashMap.newKeySet();
        final ConcurrentHashMap<WorldSnapshot.Cell,Long> revisions=new ConcurrentHashMap<>();
        final ConcurrentLinkedQueue<WorldSnapshot.Cell> requests=new ConcurrentLinkedQueue<>();
        // Everything below, except volatile diagnostics/scope, belongs exclusively to this server's thread.
        final Map<Long,Load> chunks=new LinkedHashMap<>();
        final ArrayDeque<WorldSnapshot.Cell> cacheOrder=new ArrayDeque<>();
        final Map<BlockState,Metadata> metadata=new IdentityHashMap<>();
        volatile Scope scope;
        volatile boolean closed;
        volatile int readyChunks,targetChunks,loadingChunks;
        volatile String reason="";
        long lastSchedule;
        ServerWorld world;
        BlockRenderView colorView;
        Capture capture;
        Session(IntegratedServer server,RegistryKey<World> dimension,BlockColors colors) {this.server=server;this.dimension=dimension;this.colors=colors;}
        void cleanup() {
            if(world!=null) for(Load load:chunks.values()) world.getChunkManager().removeTicket(TICKET,load.pos,0);
            chunks.clear();cache.clear();requests.clear();requested.clear();revisions.clear();cacheOrder.clear();metadata.clear();capture=null;
        }
        void service() {
            if(closed || server.isRemote()) {closed=true;cleanup();return;}
            Scope wanted=scope;if(wanted==null) return;
            world=server.getWorld(dimension);if(world==null) {reason="source dimension unavailable";return;}
            if(colorView==null) colorView=new LoadedColorView(world);
            long started=System.nanoTime();
            var manager=world.getChunkManager();
            Set<Long> desired=desiredChunks(wanted.center,wanted.radius);
            targetChunks=desired.size();
            var existing=chunks.entrySet().iterator();
            while(existing.hasNext()) {
                var entry=existing.next();Load load=entry.getValue();
                if(!desired.contains(entry.getKey())) {manager.removeTicket(TICKET,load.pos,0);existing.remove();}
                else if(started-load.refreshed>=500_000_000L) {manager.addTicket(TICKET,load.pos,0);load.refreshed=started;}
            }
            cache.keySet().removeIf(cell -> !cell.inside(wanted.center,wanted.radius,wanted.height));
            revisions.keySet().removeIf(cell -> !cell.inside(wanted.center,wanted.radius,wanted.height));
            cacheOrder.removeIf(cell -> !cache.containsKey(cell));
            int pending=(int)chunks.values().stream().filter(load -> !load.loading.isDone()).count(),created=0;
            ArrayList<Long> sorted=new ArrayList<>(desired);
            int centerX=Math.floorDiv(wanted.center.x(),2),centerZ=Math.floorDiv(wanted.center.z(),2);
            sorted.sort(Comparator.comparingLong(key -> Math.abs((long)ChunkPos.getPackedX(key)-centerX)+Math.abs((long)ChunkPos.getPackedZ(key)-centerZ)));
            for(long key:sorted) {
                if(chunks.containsKey(key)) continue;
                if(pending>=MAX_CHUNK_REQUESTS || created>=NEW_CHUNKS_PER_JOB || System.nanoTime()-started>JOB_BUDGET) break;
                ChunkPos pos=new ChunkPos(key);
                try {
                    // Unlike getChunkFutureSyncOnMainThread, this schedules generation without running tasks until FULL.
                    CompletableFuture<?> loading=manager.addChunkLoadingTicket(TICKET,pos,0);
                    chunks.put(key,new Load(pos,loading,started));pending++;created++;
                } catch(RuntimeException error) {reason="chunk request failed: "+error.getClass().getSimpleName();break;}
            }
            readyChunks=0;loadingChunks=0;
            for(Load load:chunks.values()) {
                WorldChunk chunk=manager.getWorldChunk(load.pos.x,load.pos.z);
                if(chunk!=null && chunk.isLightOn()) readyChunks++;else loadingChunks++;
            }
            int reads=0,completed=0;
            while(reads<MAX_BLOCK_READS && completed<MAX_CELLS_PER_JOB && System.nanoTime()-started<JOB_BUDGET) {
                if(capture==null) {
                    WorldSnapshot.Cell cell=requests.poll();if(cell==null) break;
                    if(!cell.inside(wanted.center,wanted.radius,wanted.height)) {requested.remove(cell);continue;}
                    WorldChunk chunk=manager.getWorldChunk(Math.floorDiv(cell.x(),2),Math.floorDiv(cell.z(),2));
                    if(chunk==null || !chunk.isLightOn()) {requests.add(cell);break;}
                    capture=new Capture(cell,revisions.getOrDefault(cell,0L));
                    int baseY=cell.y()*8;
                    if(world.isOutOfHeightLimit(baseY) || chunk.getSection(chunk.getSectionIndex(baseY)).isEmpty()) capture.cursor=512;
                }
                Capture active=capture;
                if(active.revision!=revisions.getOrDefault(active.cell,0L)) {capture=new Capture(active.cell,revisions.getOrDefault(active.cell,0L));continue;}
                if(!active.cell.inside(wanted.center,wanted.radius,wanted.height)) {requested.remove(active.cell);capture=null;continue;}
                WorldChunk chunk=manager.getWorldChunk(Math.floorDiv(active.cell.x(),2),Math.floorDiv(active.cell.z(),2));
                if(chunk==null || !chunk.isLightOn()) break;
                int batch=0;
                while(active.cursor<576 && reads<MAX_BLOCK_READS && batch<32) {
                    int index=active.cursor++;
                    int x=active.cell.x()*8+(index&7),z=active.cell.z()*8+((index>>3)&7);
                    int y=active.cell.y()*8+(index<512 ? index>>6 : 8);
                    BlockPos pos=new BlockPos(x,y,z);reads++;batch++;
                    if(index>=512) {active.top[index-512]=clampLight(world.getLightLevel(LightType.SKY,pos));continue;}
                    if(world.isOutOfHeightLimit(y)) continue;
                    BlockState state=chunk.getBlockState(pos);if(state.isAir()) continue;
                    Metadata data=metadata.computeIfAbsent(state,s -> new Metadata(Registries.BLOCK.getId(s.getBlock()).toString(),BlockGeometryCapture.stateKey(s),clampLight(s.getOpacity()),clampLight(s.getLuminance()),BlockGeometryCapture.supported(s)));
                    if(!data.supported) continue;
                    int color=state.getMapColor(world,pos).color&0xffffff;
                    // The native provider sees a read-only loaded-chunk view; boundary biome lookup cannot generate a neighbor synchronously.
                    int tint=colors.getColor(state,colorView,pos,0);if(tint!=-1) color=tint&0xffffff;
                    active.voxels.add(new Voxel(x,y,z,state.getModelOffset(pos),color,clampLight(world.getLightLevel(LightType.SKY,pos)),clampLight(world.getLightLevel(LightType.BLOCK,pos)),data));
                }
                if(active.cursor==576) {
                    cache.put(active.cell,new Snapshot(List.copyOf(active.voxels),active.top,active.revision));cacheOrder.remove(active.cell);cacheOrder.addLast(active.cell);requested.remove(active.cell);capture=null;completed++;
                    while(cacheOrder.size()>MAX_CACHED_CELLS) cache.remove(cacheOrder.removeFirst());
                }
            }
        }
    }
    private static final class LoadedColorView implements BlockRenderView {
        private final ServerWorld world;
        LoadedColorView(ServerWorld world) {this.world=world;}
        private WorldChunk chunk(BlockPos pos) {return world.getChunkManager().getWorldChunk(Math.floorDiv(pos.getX(),16),Math.floorDiv(pos.getZ(),16));}
        @Override public BlockState getBlockState(BlockPos pos) {WorldChunk chunk=chunk(pos);return chunk==null ? Blocks.AIR.getDefaultState() : chunk.getBlockState(pos);}
        @Override public FluidState getFluidState(BlockPos pos) {return getBlockState(pos).getFluidState();}
        @Override public BlockEntity getBlockEntity(BlockPos pos) {return null; /* Dedicated block-entity renderers are excluded from this terrain scope. */}
        @Override public int getHeight() {return world.getHeight();}
        @Override public int getBottomY() {return world.getBottomY();}
        @Override public float getBrightness(Direction direction,boolean shaded) {return world.getBrightness(direction,shaded);}
        @Override public LightingProvider getLightingProvider() {return world.getLightingProvider();}
        @Override public int getColor(BlockPos pos,ColorResolver resolver) {
            WorldChunk chunk=chunk(pos);if(chunk==null) return 0xffffff;
            var biome=chunk.getBiomeForNoiseGen(Math.floorDiv(pos.getX(),4),Math.floorDiv(pos.getY(),4),Math.floorDiv(pos.getZ(),4)).value();
            return resolver.getColor(biome,pos.getX(),pos.getZ());
        }
    }
    static int clampLight(int light) {return Math.max(0,Math.min(15,light));}
    static Set<Long> desiredChunks(WorldSnapshot.Cell center,int radius) {
        return SourceChunkScope.chunks(center.x(),center.z(),radius);
    }
}

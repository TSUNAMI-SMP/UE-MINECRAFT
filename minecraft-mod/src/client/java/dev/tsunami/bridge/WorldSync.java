package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import net.minecraft.client.MinecraftClient;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.Vec3d;
import net.minecraft.registry.Registries;
import java.io.IOException;
import java.util.*;

/** Bounded sampling on the client thread; reliable batches never block camera input. */
public final class WorldSync {
    private final Map<WorldSnapshot.Cell, String> confirmed = new HashMap<>();
    private final LinkedHashSet<WorldSnapshot.Cell> dirty = new LinkedHashSet<>();
    private List<WorldSnapshot.Cell> wanted = List.of();
    private final ArrayDeque<JsonObject> outgoing = new ArrayDeque<>();
    private WorldSnapshot.Cell center, sampling, sending;
    private List<WorldSnapshot.Shape> shapes;
    private String sentFingerprint;
    private int radius, height, cursor, index, synchronizedCells;
    private long generation, expiredAtStart;
    private boolean scopeNeeded, scopeInFlight, initial;

    public void reset() {
        confirmed.clear(); dirty.clear(); wanted = List.of(); outgoing.clear();
        center = sampling = sending = null; shapes = null; sentFingerprint = null;
        cursor = index = synchronizedCells = 0; generation = 0; initial=false; scopeNeeded = scopeInFlight = false;
    }
    public void beginInitial() { reset(); initial=true; }
    public boolean complete() { return !wanted.isEmpty() && confirmed.size()==wanted.size() && sending==null && !scopeInFlight && !scopeNeeded; }
    public int synchronizedCells() { return synchronizedCells; }
    public int targetCells() { return wanted.size(); }
    public void changed(BlockPos pos) {
        if(initial) return;
        // A changed block can expose a face in an adjacent cell.
        mark(WorldSnapshot.Cell.at(pos.getX(), pos.getY(), pos.getZ()));
        for (Direction d : Direction.values()) { BlockPos q = pos.offset(d); mark(WorldSnapshot.Cell.at(q.getX(), q.getY(), q.getZ())); }
    }
    private void mark(WorldSnapshot.Cell c) {
        if (center != null && c.inside(center, radius, height)) dirty.add(c);
    }
    public void tick(MinecraftClient mc, Vec3d origin, BridgeConfig config, BridgeTransport transport) throws IOException {
        if (mc.world == null || mc.player == null || origin == null || !transport.diagnostics().cameraReady()
                || !transport.diagnostics().worldSupported()) return;
        BlockPos player = mc.player.getBlockPos();
        WorldSnapshot.Cell next = WorldSnapshot.Cell.at(player.getX(), player.getY(), player.getZ());
        if (center==null || (!initial && (!next.equals(center) || radius != config.worldRadius || height != config.worldHalfHeight))) {
            center = next; radius = config.worldRadius; height = config.worldHalfHeight;
            ArrayList<WorldSnapshot.Cell> cells = new ArrayList<>();
            for (int x = -radius; x <= radius; x++) for (int z = -radius; z <= radius; z++)
                for (int y = -height; y <= height; y++) cells.add(new WorldSnapshot.Cell(center.x() + x, center.y() + y, center.z() + z));
            cells.sort(Comparator.comparingInt(c -> Math.abs(c.x()-center.x()) + Math.abs(c.z()-center.z()) + Math.abs(c.y()-center.y())));
            wanted = cells; cursor = 0; scopeNeeded = true;
            confirmed.keySet().removeIf(c -> !c.inside(center, radius, height));
            dirty.removeIf(c -> !c.inside(center, radius, height)); synchronizedCells = confirmed.size();
            if (sampling != null && !sampling.inside(center, radius, height)) { sampling = null; shapes = null; }
        }
        // Wait for all ACKs before committing a cell to the local cache. Expiry forces a full retry.
        if (sending != null || scopeInFlight) {
            for (int i = 0; i < 6 && !outgoing.isEmpty() && transport.availableEvents() > 16; i++) {
                JsonObject payload = outgoing.removeFirst(), p = transport.packet("event");
                if (generation == 0) generation = p.get("seq").getAsLong();
                payload.addProperty("snapshotSeq", generation); payload.entrySet().forEach(e -> p.add(e.getKey(), e.getValue()));
                p.addProperty("x", 0); p.addProperty("y", 0); p.addProperty("z", 0); transport.event(p);
            }
            if (!outgoing.isEmpty() || transport.diagnostics().pending() != 0) return;
            boolean success = transport.diagnostics().expired() == expiredAtStart;
            if (scopeInFlight) { if (!success) scopeNeeded = true; scopeInFlight = false; }
            if (sending != null) {
                if (success && sending.inside(center, radius, height)) confirmed.put(sending, sentFingerprint);
                else mark(sending);
                sending = null; sentFingerprint = null; synchronizedCells = confirmed.size();
            }
        }
        if (scopeNeeded) {
            if (transport.diagnostics().pending() != 0) return;
            JsonObject p = transport.packet("event"); p.addProperty("event", "world_scope"); WorldSnapshot.cellFields(p, center);
            p.addProperty("radius", radius); p.addProperty("halfHeight", height);
            p.addProperty("x", 0); p.addProperty("y", 0); p.addProperty("z", 0);
            expiredAtStart = transport.diagnostics().expired(); transport.event(p); scopeNeeded = false; scopeInFlight = true; return;
        }
        if(initial && complete()) return;
        if (sampling == null) {
            if (transport.diagnostics().pending() != 0) return;
            if (!dirty.isEmpty()) { var it = dirty.iterator(); sampling = it.next(); it.remove(); }
            else { sampling = wanted.get(cursor); cursor = (cursor+1) % wanted.size(); }
            if(initial && confirmed.containsKey(sampling)) { sampling=null; return; }
            if(initial && (!mc.world.isChunkLoaded(sampling.x()*8 >> 4,sampling.z()*8 >> 4)
                    || !mc.world.isChunkLoaded((sampling.x()*8+7) >> 4,(sampling.z()*8+7) >> 4))) {
                sampling=null; return; // Never commit unloaded chunks as empty during a one-shot import.
            }
            shapes = new ArrayList<>(); index = 0;
        }
        // 128 blocks/tick, 512 per cell. Chunk-unloaded cells are sent empty, then resampled later.
        for (int i = 0; i < 128 && index < 512; i++, index++) {
            int x = sampling.x()*8 + (index & 7), z = sampling.z()*8 + ((index >> 3) & 7), y = sampling.y()*8 + (index >> 6);
            if (Math.abs(x-origin.x)>99995 || Math.abs(y-origin.y)>99995 || Math.abs(z-origin.z)>99995) continue;
            if (!mc.world.isChunkLoaded(x >> 4, z >> 4)) continue;
            BlockPos p = new BlockPos(x,y,z); var state = mc.world.getBlockState(p);
            if (state.isAir()) continue;
            // Signs, fluids, dedicated block-entity renderers and special mechanics
            // are explicitly outside this import, never silently converted to cubes.
            if(!BlockGeometryCapture.supported(state)) continue;
            boolean buried = state.isOpaqueFullCube();
            if (buried) for (Direction d : Direction.values()) if (!mc.world.getBlockState(p.offset(d)).isOpaqueFullCube()) { buried = false; break; }
            if (buried && !initial) continue;
            int color = state.getMapColor(mc.world, p).color & 0xffffff;
            int tint=mc.getBlockColors().getColor(state,mc.world,p,0); if(tint!=-1) color=tint & 0xffffff;
            String blockId=Registries.BLOCK.getId(state.getBlock()).toString();
            if(blockId.length()>128) blockId="uebridge:unknown";
            if(!transport.blockModelsSupported()) {
                var nativeShape=state.getCollisionShape(mc.world,p);boolean collision=initial && !nativeShape.isEmpty();
                for(Box b:(collision ? nativeShape : state.getOutlineShape(mc.world,p)).getBoundingBoxes()) {
                    double sx=b.maxX-b.minX,sy=b.maxY-b.minY,sz=b.maxZ-b.minZ;
                    if(sx<=0 || sy<=0 || sz<=0 || sx>4 || sy>4 || sz>4 || shapes.size()>=WorldSnapshot.MAX_SHAPES) continue;
                    shapes.add(new WorldSnapshot.Shape(x+(b.minX+b.maxX)/2-origin.x,y+(b.minY+b.maxY)/2-origin.y,
                        z+(b.minZ+b.maxZ)/2-origin.z,color,sx,sy,sz,blockId,collision,x,y,z));
                }
                continue;
            }
            String stateKey=BlockGeometryCapture.stateKey(state);
            if(shapes.size()>=WorldSnapshot.MAX_SHAPES) break;
            // Visual geometry comes from the actual blockstate model, not its physics hull.
            var modelOffset=state.getModelOffset(p);
            shapes.add(new WorldSnapshot.Shape(x+.5+modelOffset.x-origin.x,y+.5+modelOffset.y-origin.y,z+.5+modelOffset.z-origin.z,color,1,1,1,blockId,false,x,y,z,stateKey,1));
            if(!initial) continue;
            List<Box> collisionBoxes=state.getCollisionShape(mc.world,p).getBoundingBoxes(),outlineBoxes=state.getOutlineShape(mc.world,p).getBoundingBoxes();
            for(int role:new int[]{2,3}) {
            // Identical collision/selection hulls (ordinary full blocks) need one
            // hidden proxy. The receiver enables Visibility when no role3 is present.
            if(role==3 && outlineBoxes.equals(collisionBoxes)) continue;
            for (Box b : role==2 ? collisionBoxes : outlineBoxes) {
                double sx=b.maxX-b.minX, sy=b.maxY-b.minY, sz=b.maxZ-b.minZ;
                if (sx <= 0 || sy <= 0 || sz <= 0 || sx > 4 || sy > 4 || sz > 4) continue;
                if (shapes.size() >= WorldSnapshot.MAX_SHAPES) break;
                shapes.add(new WorldSnapshot.Shape(x+(b.minX+b.maxX)/2-origin.x, y+(b.minY+b.maxY)/2-origin.y,
                        z+(b.minZ+b.maxZ)/2-origin.z, color, sx,sy,sz,blockId,role==2,x,y,z,stateKey,role));
            }
            }
        }
        if (index < 512) return;
        String fingerprint=WorldSnapshot.fingerprint(shapes);
        if (!fingerprint.equals(confirmed.get(sampling))) {
            sending = sampling; sentFingerprint = fingerprint; generation = 0;
            expiredAtStart = transport.diagnostics().expired();
            outgoing.addAll(transport.blockModelsSupported() ? WorldSnapshot.encode(sending,shapes,true,true,true)
                : WorldSnapshot.encode(sending,shapes,transport.diagnostics().texturesSupported(),initial,transport.actionsSupported()));
        }
        sampling = null; shapes = null;
    }
}

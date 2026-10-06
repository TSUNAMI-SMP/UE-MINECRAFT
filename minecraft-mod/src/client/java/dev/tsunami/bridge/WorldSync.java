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
    private boolean scopeNeeded, scopeInFlight;

    public void reset() {
        confirmed.clear(); dirty.clear(); wanted = List.of(); outgoing.clear();
        center = sampling = sending = null; shapes = null; sentFingerprint = null;
        cursor = index = synchronizedCells = 0; generation = 0; scopeNeeded = scopeInFlight = false;
    }
    public int synchronizedCells() { return synchronizedCells; }
    public int targetCells() { return wanted.size(); }
    public void changed(BlockPos pos) {
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
        if (!next.equals(center) || radius != config.worldRadius || height != config.worldHalfHeight) {
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
        if (sampling == null) {
            if (transport.diagnostics().pending() != 0) return;
            if (!dirty.isEmpty()) { var it = dirty.iterator(); sampling = it.next(); it.remove(); }
            else { sampling = wanted.get(cursor); cursor = (cursor+1) % wanted.size(); }
            shapes = new ArrayList<>(); index = 0;
        }
        // 128 blocks/tick, 512 per cell. Chunk-unloaded cells are sent empty, then resampled later.
        for (int i = 0; i < 128 && index < 512; i++, index++) {
            int x = sampling.x()*8 + (index & 7), z = sampling.z()*8 + ((index >> 3) & 7), y = sampling.y()*8 + (index >> 6);
            if (Math.abs(x-origin.x)>99995 || Math.abs(y-origin.y)>99995 || Math.abs(z-origin.z)>99995) continue;
            if (!mc.world.isChunkLoaded(x >> 4, z >> 4)) continue;
            BlockPos p = new BlockPos(x,y,z); var state = mc.world.getBlockState(p);
            if (state.isAir()) continue;
            boolean buried = state.isOpaqueFullCube();
            if (buried) for (Direction d : Direction.values()) if (!mc.world.getBlockState(p.offset(d)).isOpaqueFullCube()) { buried = false; break; }
            if (buried) continue;
            List<Box> boxes = state.getOutlineShape(mc.world, p).getBoundingBoxes();
            if (boxes.isEmpty() && !state.getFluidState().isEmpty()) boxes = List.of(new Box(0,0,0,1,state.getFluidState().getHeight(mc.world,p),1));
            int color = state.getMapColor(mc.world, p).color & 0xffffff;
            String blockId=Registries.BLOCK.getId(state.getBlock()).toString();
            if(blockId.length()>128) blockId="uebridge:unknown";
            for (Box b : boxes) {
                double sx=b.maxX-b.minX, sy=b.maxY-b.minY, sz=b.maxZ-b.minZ;
                if (sx <= 0 || sy <= 0 || sz <= 0 || sx > 4 || sy > 4 || sz > 4) continue;
                if (shapes.size() >= WorldSnapshot.MAX_SHAPES) break;
                shapes.add(new WorldSnapshot.Shape(x+(b.minX+b.maxX)/2-origin.x, y+(b.minY+b.maxY)/2-origin.y,
                        z+(b.minZ+b.maxZ)/2-origin.z, color, sx,sy,sz,blockId));
            }
        }
        if (index < 512) return;
        String fingerprint=WorldSnapshot.fingerprint(shapes);
        if (!fingerprint.equals(confirmed.get(sampling))) {
            sending = sampling; sentFingerprint = fingerprint; generation = 0;
            expiredAtStart = transport.diagnostics().expired(); outgoing.addAll(WorldSnapshot.encode(sending, shapes,transport.diagnostics().texturesSupported()));
        }
        sampling = null; shapes = null;
    }
}

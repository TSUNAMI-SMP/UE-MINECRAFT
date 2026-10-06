package dev.tsunami.bridge;

import java.util.ArrayList;
import java.util.List;

/** Match clicked TNT with a subsequently spawned primed TNT, not block removal. */
public final class IgnitionTracker {
    public record Position(int x, int y, int z) {}
    private record Candidate(Position position, long time) {}
    private final List<Candidate> candidates = new ArrayList<>();
    public void click(int x, int y, int z, long now) {
        prune(now); Position pos = new Position(x, y, z);
        candidates.removeIf(c -> c.position.equals(pos));
        if (candidates.size() >= 64) candidates.removeFirst();
        candidates.add(new Candidate(pos, now));
    }
    public Position primed(double x, double y, double z, long now) {
        prune(now);
        Candidate nearest = null; double distance = Double.POSITIVE_INFINITY;
        for (Candidate c : candidates) {
            Position p = c.position;
            double dx = x - (p.x + 0.5), dy = y - p.y, dz = z - (p.z + 0.5);
            double d = dx * dx + dy * dy + dz * dz;
            if (Math.abs(dx) <= 0.6 && Math.abs(dy) <= 0.8 && Math.abs(dz) <= 0.6 && d < distance) {
                nearest = c; distance = d;
            }
        }
        if (nearest == null) return null;
        candidates.remove(nearest); return nearest.position;
    }
    private void prune(long now) { candidates.removeIf(c -> now - c.time >= 2_000_000_000L); }
    public void clear() { candidates.clear(); }
}

package dev.tsunami.bridge;

import java.util.Set;
import java.util.UUID;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.registry.RegistryKey;
import net.minecraft.world.World;

/** In-memory tick suspension only. No entity flags/NBT/world files are modified.
 * A stalled/disconnected client automatically releases every source mob in two seconds.
 * Dedicated/remote servers and entities outside this integrated-world snapshot never match.
 */
public final class MobAuthorityLease {
    private static final long DURATION = 2_000_000_000L;
    private record Lease(IntegratedServer server, RegistryKey<World> dimension, Set<UUID> ids, long until) { }
    private static volatile Lease lease;
    private MobAuthorityLease() { }
    public static void renew(IntegratedServer server, RegistryKey<World> dimension, Set<UUID> ids) {
        lease = new Lease(server, dimension, Set.copyOf(ids), System.nanoTime() + DURATION);
    }
    public static void release() { lease = null; }
    public static boolean suspended(MobEntity mob) {
        Lease current = lease;
        if (current == null || current.server.isRemote() || System.nanoTime() >= current.until || mob.getEntityWorld().isClient()) return false;
        return mob.getEntityWorld().getServer() == current.server
            && mob.getEntityWorld().getRegistryKey().equals(current.dimension) && current.ids.contains(mob.getUuid());
    }
}

package dev.tsunami.bridge;

import java.util.*;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicBoolean;
import net.minecraft.client.MinecraftClient;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.entity.mob.HostileEntity;
import net.minecraft.entity.attribute.EntityAttributes;
import net.minecraft.registry.Registries;
import net.minecraft.util.math.Vec3d;
import net.minecraft.util.math.Box;
import net.minecraft.util.TypeFilter;

/** Native far /summon detection outside frozen-client tracking range; read-only server-thread snapshots. */
final class SourceMobAccess {
    static final int MAX_RESULTS=128;
    record Snapshot(UUID id,String type,double x,double y,double z,float yaw,float width,float height,float health,float maximum,double speed,double damage,boolean hostile,boolean baby) { }
    private final ConcurrentLinkedQueue<Snapshot> ready=new ConcurrentLinkedQueue<>();
    private final AtomicBoolean scheduled=new AtomicBoolean();
    private volatile long generation;
    void reset() {generation++;ready.clear();}
    Snapshot poll() {return ready.poll();}
    void request(MinecraftClient client,Vec3d sourceCenter,Vec3d ueCenter,int radius,int height,Set<UUID> known) {
        var server=client.getServer();if(server==null || server.isRemote() || client.world==null || !scheduled.compareAndSet(false,true)) return;
        var dimension=client.world.getRegistryKey();long expected=generation;
        var a=WorldSnapshot.Cell.at((int)Math.floor(sourceCenter.x),(int)Math.floor(sourceCenter.y),(int)Math.floor(sourceCenter.z));
        var b=WorldSnapshot.Cell.at((int)Math.floor(ueCenter.x),(int)Math.floor(ueCenter.y),(int)Math.floor(ueCenter.z));
        server.execute(()->{
            try {
                var world=server.getWorld(dimension);if(expected!=generation || server.isRemote() || world==null) return;
                List<MobEntity> nearby=new ArrayList<>();Set<UUID> found=new HashSet<>();
                java.util.function.Predicate<MobEntity> include=mob -> {
                    if(!mob.isAlive() || known.contains(mob.getUuid()) || MobBridge.unsupportedMovement(Registries.ENTITY_TYPE.getId(mob.getType()).toString())) return false;
                    var cell=WorldSnapshot.Cell.at(mob.getBlockX(),mob.getBlockY(),mob.getBlockZ());
                    return (cell.inside(a,radius,height) || cell.inside(b,radius,height)) && found.add(mob.getUuid());
                };
                // The server's spatial entity index limits work to these loaded scopes, not all loaded dimensions/entities.
                world.collectEntitiesByType(TypeFilter.instanceOf(MobEntity.class),box(a,radius,height),include,nearby,MAX_RESULTS);
                if(nearby.size()<MAX_RESULTS && !a.equals(b)) world.collectEntitiesByType(TypeFilter.instanceOf(MobEntity.class),box(b,radius,height),include,nearby,MAX_RESULTS);
                List<Snapshot> captured=new ArrayList<>(nearby.size());
                for(MobEntity mob:nearby) {
                    String type=Registries.ENTITY_TYPE.getId(mob.getType()).toString();
                    double speed=mob.getAttributes().hasAttribute(EntityAttributes.MOVEMENT_SPEED) ? mob.getAttributeValue(EntityAttributes.MOVEMENT_SPEED) : .25;
                    double damage=mob.getAttributes().hasAttribute(EntityAttributes.ATTACK_DAMAGE) ? mob.getAttributeValue(EntityAttributes.ATTACK_DAMAGE) : 0;
                    captured.add(new Snapshot(mob.getUuid(),type,mob.getX(),mob.getY(),mob.getZ(),mob.getYaw(),mob.getWidth(),mob.getHeight(),mob.getHealth(),mob.getMaxHealth(),speed,damage,mob instanceof HostileEntity,mob.isBaby()));
                }
                if(expected==generation) for(Snapshot snapshot:captured) {if(ready.size()>=MAX_RESULTS) break;ready.add(snapshot);}
            } finally {scheduled.set(false);}
        });
    }
    private static Box box(WorldSnapshot.Cell cell,int radius,int height) {
        return new Box((cell.x()-radius)*8.0,(cell.y()-height)*8.0,(cell.z()-radius)*8.0,(cell.x()+radius+1)*8.0,(cell.y()+height+1)*8.0,(cell.z()+radius+1)*8.0);
    }
}

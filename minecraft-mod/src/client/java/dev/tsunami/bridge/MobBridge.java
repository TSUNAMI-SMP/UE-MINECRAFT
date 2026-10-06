package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.IOException;
import java.nio.file.*;
import java.util.*;
import net.minecraft.client.MinecraftClient;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.entity.mob.HostileEntity;
import net.minecraft.entity.attribute.EntityAttributes;
import net.minecraft.util.math.Vec3d;

/** One snapshot per imported world. Minecraft source mobs are never updated from UE.
 * Reliable spawn receipts precede the reversible, in-memory source-tick lease.
 */
public final class MobBridge {
    private String imported="", status="未取り込み";
    private final ArrayDeque<JsonObject> pending=new ArrayDeque<>();
    private final Set<UUID> ids=new HashSet<>();
    private long expiredAtStart;
    private boolean sent, acknowledged, failed;
    public String status() { return status; }
    public void release() { MobAuthorityLease.release();pending.clear();ids.clear();imported="";sent=acknowledged=failed=false;status="停止"; }
    public void pause() { MobAuthorityLease.release(); }
    public void tick(MinecraftClient client, BridgeTransport transport,Vec3d origin,String importId,boolean enabled,int radius,int halfHeight) throws IOException {
        if(!enabled || client.isPaused() || client.getServer()==null || client.getServer().isRemote() || client.world==null || client.player==null
            || origin==null || !transport.diagnostics().connected() || !transport.diagnostics().ueControl() || transport.authorityPose()==null) {
            pause();if(client.getServer()==null) status="シングルプレイ専用（既存サーバーには適用しません）";return;
        }
        if(!transport.mobsSupported()) { pause();status="UEモブ機能が未準備";return; }
        if(importId==null || importId.isEmpty() || !transport.diagnostics().worldSealed()) { pause();return; }
        if(failed && imported.equals(importId)) { pause();return; }
        if(!imported.equals(importId)) {
            release();imported=importId;expiredAtStart=transport.diagnostics().expired();
            Map<String,String> loaded=loadLatest(client.runDirectory.toPath().resolve("uebridge-export"));
            int missing=0,excluded=0;
            // Match the imported 8-block cells exactly. The frozen MC player marks the snapshot center.
            int cx=Math.floorDiv(client.player.getBlockX(),8),cy=Math.floorDiv(client.player.getBlockY(),8),cz=Math.floorDiv(client.player.getBlockZ(),8);
            for(var entity:client.world.getEntities()) {
                if(!(entity instanceof MobEntity mob) || !mob.isAlive()) continue;
                int x=Math.floorDiv(mob.getBlockX(),8),y=Math.floorDiv(mob.getBlockY(),8),z=Math.floorDiv(mob.getBlockZ(),8);
                if(Math.abs(x-cx)>radius || Math.abs(y-cy)>halfHeight || Math.abs(z-cz)>radius) continue;
                String type=net.minecraft.registry.Registries.ENTITY_TYPE.getId(mob.getType()).toString();
                // No fluid/flight gameplay in this ground-movement implementation.
                if(unsupportedMovement(type)) { excluded++;continue; }
                String appearance=MobModelExport.appearance(mob.getUuid());if(appearance==null) appearance=loaded.get(mob.getUuidAsString());
                if(appearance==null) { missing++;continue; }
                if(ids.size()>=MobModelExport.MAX_MOBS) { excluded++;continue; }
                JsonObject packet=new JsonObject();packet.addProperty("event","mob_spawn");packet.addProperty("importId",importId);
                packet.addProperty("mobId",mob.getUuidAsString());packet.addProperty("mobType",type);packet.addProperty("appearance",appearance);
                packet.addProperty("x",mob.getX()-origin.x);packet.addProperty("y",mob.getY()-origin.y);packet.addProperty("z",mob.getZ()-origin.z);
                packet.addProperty("yaw",mob.getYaw());packet.addProperty("width",mob.getWidth());packet.addProperty("height",mob.getHeight());
                packet.addProperty("health",mob.getHealth());packet.addProperty("maxHealth",mob.getMaxHealth());
                double speed=mob.getAttributes().hasAttribute(EntityAttributes.MOVEMENT_SPEED) ? mob.getAttributeValue(EntityAttributes.MOVEMENT_SPEED) : .25;
                double damage=mob.getAttributes().hasAttribute(EntityAttributes.ATTACK_DAMAGE) ? mob.getAttributeValue(EntityAttributes.ATTACK_DAMAGE) : 0;
                packet.addProperty("speed",Math.max(0,Math.min(2,speed)));packet.addProperty("damage",Math.max(0,Math.min(100,damage)));
                packet.addProperty("hostile",mob instanceof HostileEntity);packet.addProperty("baby",mob.isBaby());
                pending.add(packet);ids.add(mob.getUuid());
            }
            status="対象="+ids.size()+" 素材未準備="+missing+" 飛行/水中/上限除外="+excluded;
        }
        for(int i=0;i<4 && !pending.isEmpty() && transport.availableEvents()>16;i++) {
            JsonObject event=transport.packet("event");pending.getFirst().entrySet().forEach(e->event.add(e.getKey(),e.getValue()));transport.event(event);pending.removeFirst();
        }
        if(!pending.isEmpty()) return;
        sent=true;
        if(!acknowledged && transport.diagnostics().pending()==0) {
            acknowledged=transport.diagnostics().expired()==expiredAtStart;
            if(!acknowledged) { failed=true;status += " / 送信期限切れ。取り込みをやり直してください"; }
        }
        if(sent && acknowledged) MobAuthorityLease.renew(client.getServer(),client.world.getRegistryKey(),ids);
        else pause();
    }
    static boolean unsupportedMovement(String id) {
        return Set.of("minecraft:bat","minecraft:bee","minecraft:allay","minecraft:vex","minecraft:ghast","minecraft:happy_ghast","minecraft:blaze","minecraft:breeze","minecraft:phantom","minecraft:ender_dragon",
            "minecraft:cod","minecraft:salmon","minecraft:tropical_fish","minecraft:pufferfish","minecraft:squid","minecraft:glow_squid","minecraft:dolphin","minecraft:axolotl","minecraft:guardian","minecraft:elder_guardian").contains(id);
    }
    private static Map<String,String> loadLatest(Path root) {
        if(!Files.isDirectory(root)) return Map.of();
        try(var directories=Files.list(root)) {
            var latest=directories.filter(p->p.getFileName().toString().startsWith("mobs-") && Files.isRegularFile(p.resolve("manifest.json")))
                .max(Comparator.comparing(p->p.getFileName().toString()));
            if(latest.isEmpty()) return Map.of();Path file=latest.get().resolve("manifest.json");if(Files.size(file)>32*1024*1024) return Map.of();
            JsonObject manifest=JsonParser.parseString(Files.readString(file)).getAsJsonObject();
            if(!"mobs".equals(manifest.get("kind").getAsString()) || manifest.get("version").getAsInt()!=1) return Map.of();
            Map<String,String> result=new HashMap<>();
            for(var entry:manifest.getAsJsonObject("entities").entrySet()) {
                String key=entry.getValue().getAsString();UUID.fromString(entry.getKey());
                if(!key.matches("[0-9a-f]{64}")) return Map.of();result.put(entry.getKey(),key);
            }
            return result;
        } catch(IOException | RuntimeException invalid) { return Map.of(); }
    }
}

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

/** Detects new native /summon entities after import, retaining a per-UUID spawn tombstone.
 * A source mob is leased only after its own successful UE spawn receipt, never a global ACK count.
 */
public final class MobBridge {
    private String imported="", receiver="", status="未取り込み", lastFailure="";
    private final MobTransferLedger ledger=new MobTransferLedger();
    private final Map<UUID,JsonObject> snapshots=new HashMap<>();
    private final SourceMobAccess serverMobs=new SourceMobAccess();
    private Models models=new Models(Map.of(),Map.of());
    private long lastScan, lastModels;
    private int missing, excluded;
    public String status() { return status; }
    public void release() { MobAuthorityLease.release();serverMobs.reset();ledger.clear();snapshots.clear();imported=receiver="";lastScan=lastModels=0;lastFailure="";status="停止"; }
    public void pause() { MobAuthorityLease.release(); }
    public void tick(MinecraftClient client, BridgeTransport transport,Vec3d origin,String importId,boolean enabled,int radius,int halfHeight) throws IOException {
        if(!enabled || client.isPaused() || client.getServer()==null || client.getServer().isRemote() || client.world==null || client.player==null
            || origin==null || !transport.diagnostics().connected() || !transport.diagnostics().ueControl() || transport.authorityPose()==null) {
            pause();if(client.getServer()==null) status="シングルプレイ専用（既存サーバーには適用しません）";return;
        }
        if(!transport.mobsSupported()) { pause();status="UEモブ機能が未準備";return; }
        if(importId==null || importId.isEmpty() || !transport.diagnostics().worldSealed()) { pause();return; }
        final long now=System.nanoTime();
        if(!imported.equals(importId) || !receiver.equals(transport.diagnostics().receiverId())) {
            release();imported=importId;receiver=transport.diagnostics().receiverId();
        }
        if(lastModels==0 || now-lastModels>=5_000_000_000L) { models=loadLatest(client.runDirectory.toPath().resolve("uebridge-export"));lastModels=now; }
        if(lastScan==0 || now-lastScan>=250_000_000L) {
            scan(client,transport,origin,importId,radius,halfHeight);
            var pose=transport.authorityPose();
            serverMobs.request(client,client.player.getEntityPos(),origin.add(pose.x(),pose.y(),pose.z()),radius,halfHeight,ledger.history());lastScan=now;
        }
        SourceMobAccess.Snapshot source;
        while((source=serverMobs.poll())!=null) captureSource(source,origin,importId);
        int budget=4;
        for(MobTransferLedger.Transfer transfer:ledger.transfers()) {
            if(transfer.state==MobTransferLedger.State.IN_FLIGHT) {
                var receipt=transport.eventReceipt(transfer.eventId);
                if(receipt!=null) {
                    ledger.receipt(transfer,receipt.accepted(),receipt.reason());
                    if(!receipt.accepted()) lastFailure=receipt.reason();
                } else if(!transport.eventPending(transfer.eventId)) ledger.expired(transfer,now);
            }
            if(transfer.state!=MobTransferLedger.State.QUEUED || now<transfer.retryAt || budget<=0 || transport.availableEvents()<=16) continue;
            JsonObject snapshot=snapshots.get(transfer.id);if(snapshot==null) continue;
            JsonObject event=transport.packet("event");snapshot.entrySet().forEach(e->event.add(e.getKey(),e.getValue()));
            transport.event(event);ledger.sent(transfer,event.get("eventId").getAsString());budget--;
        }
        Set<UUID> accepted=ledger.accepted();
        if(accepted.isEmpty()) pause();else MobAuthorityLease.renew(client.getServer(),client.world.getRegistryKey(),accepted);
        int rejected=ledger.count(MobTransferLedger.State.REJECTED),expired=ledger.count(MobTransferLedger.State.EXPIRED);
        status="UE生成済="+accepted.size()+" 送信待ち="+(ledger.count(MobTransferLedger.State.QUEUED)+ledger.count(MobTransferLedger.State.IN_FLIGHT))
            +" 素材未準備="+missing+" 除外="+excluded+" 拒否="+rejected+" 期限切れ="+expired
            +(lastFailure.isEmpty() ? "" : " / 理由="+lastFailure);
    }
    private void captureSource(SourceMobAccess.Snapshot mob,Vec3d origin,String importId) {
        if(ledger.contains(mob.id())) return;
        String appearance=MobModelExport.appearance(mob.id());if(appearance==null) appearance=models.entities().get(mob.id().toString());
        if(appearance==null && !mob.baby()) appearance=models.templates().get(mob.type());
        if(appearance==null) {missing++;return;}
        if(!ledger.add(mob.id())) {excluded++;return;}
        JsonObject packet=new JsonObject();packet.addProperty("event","mob_spawn");packet.addProperty("importId",importId);
        packet.addProperty("mobId",mob.id().toString());packet.addProperty("mobType",mob.type());packet.addProperty("appearance",appearance);
        packet.addProperty("x",mob.x()-origin.x);packet.addProperty("y",mob.y()-origin.y);packet.addProperty("z",mob.z()-origin.z);
        packet.addProperty("yaw",mob.yaw());packet.addProperty("width",mob.width());packet.addProperty("height",mob.height());
        packet.addProperty("health",mob.health());packet.addProperty("maxHealth",mob.maximum());
        packet.addProperty("speed",Math.max(0,Math.min(2,mob.speed())));packet.addProperty("damage",Math.max(0,Math.min(100,mob.damage())));
        packet.addProperty("hostile",mob.hostile());packet.addProperty("baby",mob.baby());snapshots.put(mob.id(),packet);
    }
    private void scan(MinecraftClient client,BridgeTransport transport,Vec3d origin,String importId,int radius,int halfHeight) {
        missing=excluded=0;
        var pose=transport.authorityPose();
        // /summon ~ uses the frozen MC player's coordinates. Preserve exact explicit MC positions.
        int cx=Math.floorDiv(client.player.getBlockX(),8),cy=Math.floorDiv(client.player.getBlockY(),8),cz=Math.floorDiv(client.player.getBlockZ(),8);
        int ux=(int)Math.floor((origin.x+pose.x())/8),uy=(int)Math.floor((origin.y+pose.y())/8),uz=(int)Math.floor((origin.z+pose.z())/8);
        for(var entity:client.world.getEntities()) {
            if(!(entity instanceof MobEntity mob) || !mob.isAlive() || ledger.contains(mob.getUuid())) continue;
            int x=Math.floorDiv(mob.getBlockX(),8),y=Math.floorDiv(mob.getBlockY(),8),z=Math.floorDiv(mob.getBlockZ(),8);
            if(!inScope(x,y,z,cx,cy,cz,radius,halfHeight) && !inScope(x,y,z,ux,uy,uz,radius,halfHeight)) continue;
            String type=net.minecraft.registry.Registries.ENTITY_TYPE.getId(mob.getType()).toString();
            if(unsupportedMovement(type)) { excluded++;continue; }
            String appearance=MobModelExport.appearance(mob.getUuid());if(appearance==null) appearance=models.entities().get(mob.getUuidAsString());
            // A species default is appropriate for a newly summoned adult. Baby geometry needs its actual export.
            if(appearance==null && !mob.isBaby()) appearance=models.templates().get(type);
            if(appearance==null) { missing++;continue; }
            if(!ledger.add(mob.getUuid())) { excluded++;continue; }
            JsonObject packet=new JsonObject();packet.addProperty("event","mob_spawn");packet.addProperty("importId",importId);
            packet.addProperty("mobId",mob.getUuidAsString());packet.addProperty("mobType",type);packet.addProperty("appearance",appearance);
            packet.addProperty("x",mob.getX()-origin.x);packet.addProperty("y",mob.getY()-origin.y);packet.addProperty("z",mob.getZ()-origin.z);
            packet.addProperty("yaw",mob.getYaw());packet.addProperty("width",mob.getWidth());packet.addProperty("height",mob.getHeight());
            packet.addProperty("health",mob.getHealth());packet.addProperty("maxHealth",mob.getMaxHealth());
            double speed=mob.getAttributes().hasAttribute(EntityAttributes.MOVEMENT_SPEED) ? mob.getAttributeValue(EntityAttributes.MOVEMENT_SPEED) : .25;
            double damage=mob.getAttributes().hasAttribute(EntityAttributes.ATTACK_DAMAGE) ? mob.getAttributeValue(EntityAttributes.ATTACK_DAMAGE) : 0;
            packet.addProperty("speed",Math.max(0,Math.min(2,speed)));packet.addProperty("damage",Math.max(0,Math.min(100,damage)));
            packet.addProperty("hostile",mob instanceof HostileEntity);packet.addProperty("baby",mob.isBaby());snapshots.put(mob.getUuid(),packet);
        }
    }
    static boolean inScope(int x,int y,int z,int cx,int cy,int cz,int radius,int halfHeight) {
        return Math.abs((long)x-cx)<=radius && Math.abs((long)y-cy)<=halfHeight && Math.abs((long)z-cz)<=radius;
    }
    static boolean unsupportedMovement(String id) {
        return Set.of("minecraft:bat","minecraft:bee","minecraft:allay","minecraft:vex","minecraft:ghast","minecraft:happy_ghast","minecraft:blaze","minecraft:breeze","minecraft:phantom","minecraft:ender_dragon",
            "minecraft:cod","minecraft:salmon","minecraft:tropical_fish","minecraft:pufferfish","minecraft:squid","minecraft:glow_squid","minecraft:dolphin","minecraft:axolotl","minecraft:guardian","minecraft:elder_guardian").contains(id);
    }
    record Models(Map<String,String> entities,Map<String,String> templates) { }
    static Models parseModels(JsonObject manifest) {
        if(!manifest.has("kind") || !"mobs".equals(manifest.get("kind").getAsString()) || manifest.get("version").getAsInt()!=1) throw new IllegalArgumentException("Unsupported mob manifest");
        Map<String,String> entities=new HashMap<>(),templates=new HashMap<>();
        JsonObject appearances=manifest.getAsJsonObject("appearances");
        for(var entry:manifest.getAsJsonObject("entities").entrySet()) {
            String key=entry.getValue().getAsString();UUID.fromString(entry.getKey());
            if(!key.matches("[0-9a-f]{64}") || !appearances.has(key)) throw new IllegalArgumentException("Invalid mob appearance");entities.put(entry.getKey(),key);
        }
        if(manifest.has("templates")) for(var entry:manifest.getAsJsonObject("templates").entrySet()) {
            String key=entry.getValue().getAsString();
            if(!entry.getKey().matches("minecraft:[a-z0-9_]+") || !key.matches("[0-9a-f]{64}") || !appearances.has(key)
                || !entry.getKey().equals(appearances.getAsJsonObject(key).get("type").getAsString())) throw new IllegalArgumentException("Invalid mob template");
            templates.put(entry.getKey(),key);
        }
        // Older nearby-only exports still provide useful adult species defaults.
        for(var entry:appearances.entrySet()) {
            JsonObject appearance=entry.getValue().getAsJsonObject();String type=appearance.get("type").getAsString();
            boolean baby=appearance.has("stats") && appearance.getAsJsonObject("stats").has("baby") && appearance.getAsJsonObject("stats").get("baby").getAsBoolean();
            if(!baby && entry.getKey().matches("[0-9a-f]{64}") && type.matches("minecraft:[a-z0-9_]+")) templates.putIfAbsent(type,entry.getKey());
        }
        return new Models(Map.copyOf(entities),Map.copyOf(templates));
    }
    private static Models loadLatest(Path root) {
        if(!Files.isDirectory(root)) return new Models(Map.of(),Map.of());
        try(var directories=Files.list(root)) {
            var latest=directories.filter(p->p.getFileName().toString().startsWith("mobs-") && Files.isRegularFile(p.resolve("manifest.json")))
                .max(Comparator.comparing(p->p.getFileName().toString()));
            if(latest.isEmpty()) return new Models(Map.of(),Map.of());Path file=latest.get().resolve("manifest.json");if(Files.size(file)>32*1024*1024) return new Models(Map.of(),Map.of());
            return parseModels(JsonParser.parseString(Files.readString(file)).getAsJsonObject());
        } catch(IOException | RuntimeException invalid) { return new Models(Map.of(),Map.of()); }
    }
}

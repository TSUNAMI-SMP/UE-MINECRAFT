package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import java.util.LinkedHashMap;
import java.util.Set;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.sound.PositionedSoundInstance;
import net.minecraft.registry.Registries;
import net.minecraft.sound.SoundCategory;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.Vec3d;
import net.minecraft.util.math.random.Random;

/** Resource-pack mob sound playback only. UE health does not modify the saved MC player. */
public final class MobFeedback {
    private final LinkedHashMap<String,Boolean> seen=new LinkedHashMap<>();
    public void reset() { seen.clear(); }
    public static boolean valid(JsonObject p) { return MobFeedbackData.valid(p); }
    public boolean accept(MinecraftClient mc,JsonObject packet) {
        if(!valid(packet) || mc.world==null || mc.player==null) return false;
        String key=packet.get("session").getAsString()+":"+packet.get("receiverId").getAsString()+":"+packet.get("effectId").getAsString();
        if(seen.putIfAbsent(key,true)!=null) return false;if(seen.size()>256) seen.remove(seen.keySet().iterator().next());
        Identifier id=Identifier.tryParse(packet.get("sound").getAsString());
        if(id==null || !Registries.SOUND_EVENT.containsId(id)) return false;
        var listener=mc.getSoundManager().getListenerTransform();Vec3d forward=listener.forward().normalize();
        Vec3d right=forward.crossProduct(listener.up()).normalize();Vec3d up=right.crossProduct(forward).normalize();
        Vec3d location=listener.position().add(right.multiply(packet.get("lx").getAsDouble())).add(up.multiply(packet.get("ly").getAsDouble())).add(forward.multiply(packet.get("lz").getAsDouble()));
        String path=id.getPath();
        String species=path.startsWith("entity.") ? path.substring(7).split("\\.",2)[0] : "";
        SoundCategory category=species.equals("player") ? SoundCategory.PLAYERS :
            Set.of("zombie","husk","drowned","skeleton","stray","bogged","wither_skeleton","creeper","spider","cave_spider","enderman","silverfish","endermite",
                "slime","magma_cube","witch","pillager","vindicator","evoker","ravager","warden","hoglin","zoglin","piglin_brute","wither").contains(species)
            ? SoundCategory.HOSTILE : SoundCategory.NEUTRAL;
        mc.getSoundManager().play(new PositionedSoundInstance(Registries.SOUND_EVENT.get(id),category,1f,1f,Random.create(),location.x,location.y,location.z));
        return true;
    }
}

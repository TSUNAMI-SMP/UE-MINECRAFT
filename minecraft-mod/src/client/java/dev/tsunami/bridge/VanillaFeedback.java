package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import java.util.LinkedHashMap;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.sound.PositionedSoundInstance;
import net.minecraft.client.sound.SoundListenerTransform;
import net.minecraft.registry.Registries;
import net.minecraft.sound.BlockSoundGroup;
import net.minecraft.sound.SoundCategory;
import net.minecraft.sound.SoundEvent;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.Vec3d;
import net.minecraft.util.math.random.Random;

/** Uses the active Minecraft sound pack; effects are emitted only after an authoritative UE result. */
public final class VanillaFeedback {
    private final LinkedHashMap<String,Boolean> seen=new LinkedHashMap<>();
    private final Random random=Random.create();
    public void reset() { seen.clear(); }

    /** Main-thread only. Returns false for invalid/duplicate/unavailable packets. */
    public boolean accept(MinecraftClient client,JsonObject packet) {
        VanillaFeedbackData effect=VanillaFeedbackData.parse(packet);
        if (effect==null || client.player==null || client.world==null) return false;
        String key=effect.session()+":"+effect.receiverId()+":"+effect.effectId();
        if (seen.putIfAbsent(key,Boolean.TRUE)!=null) return false;
        if (seen.size()>256) seen.remove(seen.keySet().iterator().next());
        Identifier blockId=Identifier.tryParse(effect.block());
        if (blockId==null || !Registries.BLOCK.containsId(blockId)) return false;
        var state=Registries.BLOCK.get(blockId).getDefaultState();
        if (state.isAir()) return false;
        // Vanilla has no extra thud on normal jumps, or fall damage sounds in creative mode.
        if (effect.type()==VanillaFeedbackData.Type.LAND && (effect.fallDistance()<=3 || client.player.isCreative())) return true;
        BlockSoundGroup group=state.getSoundGroup();
        SoundEvent sound=switch(effect.type()) {
            case BREAK -> group.getBreakSound(); case PLACE -> group.getPlaceSound();
            case STEP -> group.getStepSound(); case LAND -> group.getFallSound();
        };
        SoundCategory category=effect.type()==VanillaFeedbackData.Type.BREAK || effect.type()==VanillaFeedbackData.Type.PLACE
            ? SoundCategory.BLOCKS : SoundCategory.PLAYERS;
        Vec3d soundPosition=rebase(effect,client.getSoundManager().getListenerTransform());
        client.getSoundManager().play(new PositionedSoundInstance(sound,category,effect.volume(group.getVolume()),
            effect.pitch(group.getPitch()),random,soundPosition.x,soundPosition.y,soundPosition.z));
        return true;
    }

    private static Vec3d rebase(VanillaFeedbackData effect,SoundListenerTransform minecraftListener) {
        // Minecraft's body remains fixed in UE-control mode. Mapping camera-relative offsets onto the
        // actual audio listener preserves attenuation/stereo position in first person and both third-person views.
        VanillaFeedbackData.Point local=effect.listenerSpace();
        Vec3d forward=minecraftListener.forward().normalize();
        Vec3d right=forward.crossProduct(minecraftListener.up()).normalize();
        Vec3d up=right.crossProduct(forward).normalize();
        return minecraftListener.position().add(right.multiply(local.x())).add(up.multiply(local.y())).add(forward.multiply(local.z()));
    }
}

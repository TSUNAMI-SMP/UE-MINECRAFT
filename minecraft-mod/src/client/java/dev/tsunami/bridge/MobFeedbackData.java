package dev.tsunami.bridge;

import com.google.gson.JsonObject;

/** Pure protocol validation shared by transport and Minecraft playback. */
public final class MobFeedbackData {
    private MobFeedbackData() { }
    public static boolean valid(JsonObject packet) {
        try {
            for(String field:new String[]{"type","sound","effectId","session","receiverId"}) {
                if(!packet.has(field) || !packet.get(field).isJsonPrimitive() || !packet.getAsJsonPrimitive(field).isString()) return false;
                String value=packet.get(field).getAsString();if(value.isEmpty() || value.length()>(field.equals("sound") ? 160 : 96)) return false;
            }
            if(!"mob".equals(packet.get("type").getAsString())) return false;
            String sound=packet.get("sound").getAsString();
            if(!sound.matches("[a-z0-9_.-]+:[a-z0-9_./-]+") || sound.contains("..") || sound.contains(":/")) return false;
            for(String field:new String[]{"lx","ly","lz"}) {
                if(!packet.has(field) || !packet.get(field).isJsonPrimitive() || !packet.getAsJsonPrimitive(field).isNumber()) return false;
                double value=packet.get(field).getAsDouble();if(!Double.isFinite(value) || Math.abs(value)>100000) return false;
            }
            return true;
        } catch(RuntimeException invalid) { return false; }
    }
}

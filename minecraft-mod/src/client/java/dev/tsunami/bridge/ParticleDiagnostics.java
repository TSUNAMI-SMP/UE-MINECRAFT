package dev.tsunami.bridge;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;

/** Optional status extension. Bad diagnostics cannot invalidate otherwise valid UE readiness. */
public record ParticleDiagnostics(boolean supported, boolean ready, String reason, boolean materialReady,
                                  int textureCount, long requested, long spawned, long rejected,
                                  int active, int instances, int peakInstances, int groups, String lastType, String lastBlock,
                                  int lastRequested, int lastSpawned, String lastReason) {
    private static final long MAX_SAFE_INTEGER = 9_007_199_254_740_991L;

    public static ParticleDiagnostics unavailable(String reason) {
        return new ParticleDiagnostics(false,false,reason,false,0,0,0,0,0,0,0,0,"","",0,0,"");
    }

    public static ParticleDiagnostics parse(JsonObject status) {
        if (!status.has("particles")) return unavailable("unsupported");
        try {
            JsonElement extension = status.get("particles");
            if (!extension.isJsonObject()) return unavailable("invalid-diagnostics");
            JsonObject p = extension.getAsJsonObject();
            boolean ready = bool(status,"particlesReady");
            String block = text(p,"lastBlock",128);
            if (!block.isEmpty() && !block.matches("[a-z0-9_.-]+:[a-z0-9_./-]+"))
                return unavailable("invalid-diagnostics");
            return new ParticleDiagnostics(true,ready,token(p,"reason",64,false),bool(p,"materialReady"),
                (int)integer(p,"textureCount",4096),integer(p,"requested",MAX_SAFE_INTEGER),
                integer(p,"spawned",MAX_SAFE_INTEGER),integer(p,"rejected",MAX_SAFE_INTEGER),
                (int)integer(p,"active",1_000_000),(int)integer(p,"instances",1_000_000),
                p.has("peakInstances") ? (int)integer(p,"peakInstances",1_000_000) : 0,
                (int)integer(p,"groups",4096),token(p,"lastType",32,true),block,
                (int)integer(p,"lastRequested",4096),(int)integer(p,"lastSpawned",4096),
                token(p,"lastReason",64,true));
        } catch (IllegalArgumentException | IllegalStateException ex) {
            return unavailable("invalid-diagnostics");
        }
    }

    private static long integer(JsonObject p,String key,long maximum) {
        JsonElement value = p.get(key);
        if (value==null || !value.isJsonPrimitive() || !value.getAsJsonPrimitive().isNumber())
            throw new IllegalArgumentException("Expected diagnostic integer");
        double number = value.getAsDouble();
        if (!Double.isFinite(number) || number!=Math.rint(number) || number<0 || number>maximum)
            throw new IllegalArgumentException("Diagnostic integer out of range");
        return (long)number;
    }
    private static boolean bool(JsonObject p,String key) {
        JsonElement value = p.get(key);
        if (value==null || !value.isJsonPrimitive() || !value.getAsJsonPrimitive().isBoolean())
            throw new IllegalArgumentException("Expected diagnostic boolean");
        return value.getAsBoolean();
    }
    private static String text(JsonObject p,String key,int maximum) {
        JsonElement value = p.get(key);
        if (value==null || !value.isJsonPrimitive() || !value.getAsJsonPrimitive().isString())
            throw new IllegalArgumentException("Expected diagnostic string");
        String string = value.getAsString();
        if (string.length()>maximum) throw new IllegalArgumentException("Diagnostic string too long");
        return string;
    }
    private static String token(JsonObject p,String key,int maximum,boolean emptyAllowed) {
        String token = text(p,key,maximum);
        if (!(emptyAllowed && token.isEmpty()) && !token.matches("[a-z0-9_-]+"))
            throw new IllegalArgumentException("Invalid diagnostic token");
        return token;
    }

    public String summary() {
        if (!supported) return switch(reason) {
            case "unsupported" -> "粒子診断=旧UE非対応";
            case "disconnected" -> "粒子診断=応答待ち";
            default -> "粒子診断=無効な診断データ";
        };
        return "粒子="+ready+" 素材="+materialReady+" 粒子テクスチャ="+textureCount
            +" 要求="+requested+" 生成="+spawned+" 拒否="+rejected
            +" 生存="+active+" 登録="+instances+" 最大登録="+peakInstances+" 組="+groups
            +" 準備理由="+reason+" 最終="+lastType+" "+lastRequested+"→"+lastSpawned
            +" 理由="+lastReason+(lastBlock.isEmpty() ? "" : " 対象="+lastBlock);
    }
}

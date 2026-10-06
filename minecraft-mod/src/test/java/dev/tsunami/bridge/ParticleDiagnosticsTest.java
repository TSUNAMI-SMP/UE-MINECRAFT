package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import org.junit.Test;
import static org.junit.Assert.*;

public class ParticleDiagnosticsTest {
    static JsonObject status() {
        JsonObject status = new JsonObject(), p = new JsonObject();
        status.addProperty("particlesReady",true); status.add("particles",p);
        p.addProperty("reason","ready"); p.addProperty("materialReady",true); p.addProperty("textureCount",42);
        p.addProperty("requested",70); p.addProperty("spawned",64); p.addProperty("rejected",6);
        p.addProperty("active",60); p.addProperty("instances",60); p.addProperty("peakInstances",64); p.addProperty("groups",1);
        p.addProperty("lastType","break"); p.addProperty("lastBlock","minecraft:stone");
        p.addProperty("lastRequested",64); p.addProperty("lastSpawned",64); p.addProperty("lastReason","spawned");
        return status;
    }
    @Test public void displaysGenerationAndRenderingSeparatelyToFindSilentFailure() {
        JsonObject status = status(); status.getAsJsonObject("particles").addProperty("instances",0);
        ParticleDiagnostics parsed = ParticleDiagnostics.parse(status);
        assertTrue(parsed.supported()); assertTrue(parsed.ready()); assertEquals(42,parsed.textureCount());
        assertEquals(64,parsed.spawned()); assertEquals(0,parsed.instances());
        assertTrue(parsed.summary().contains("64→64")); assertTrue(parsed.summary().contains("登録=0"));
        assertEquals(64,parsed.peakInstances());
    }
    @Test public void legacyAndMissingTextureDiagnosticsRemainDistinct() {
        assertFalse(ParticleDiagnostics.parse(new JsonObject()).supported());
        assertEquals("unsupported",ParticleDiagnostics.parse(new JsonObject()).reason());
        JsonObject status = status(); status.addProperty("particlesReady",false);
        JsonObject p = status.getAsJsonObject("particles"); p.addProperty("textureCount",0);
        p.addProperty("reason","missing-textures"); p.addProperty("lastReason","missing-texture");
        p.addProperty("lastSpawned",0);
        var parsed = ParticleDiagnostics.parse(status);
        assertTrue(parsed.supported()); assertFalse(parsed.ready());
        assertTrue(parsed.summary().contains("64→0")); assertTrue(parsed.summary().contains("missing-texture"));
    }
    @Test public void rejectsWrongTypesFractionsNegativeAndUnsafeCounters() {
        for(String field:new String[]{"requested","spawned","rejected","active","instances","peakInstances","groups","textureCount","lastRequested","lastSpawned"}) {
            for(double bad:new double[]{-1,.5,Double.NaN,Double.POSITIVE_INFINITY,9_007_199_254_740_992.0}) {
                JsonObject status = status(); status.getAsJsonObject("particles").addProperty(field,bad);
                assertFalse(field+"="+bad,ParticleDiagnostics.parse(status).supported());
            }
            JsonObject status = status(); status.getAsJsonObject("particles").addProperty(field,"10");
            assertFalse(ParticleDiagnostics.parse(status).supported());
        }
        JsonObject status = status(); status.addProperty("particlesReady","true");
        assertFalse(ParticleDiagnostics.parse(status).supported());
    }
    @Test public void acceptsLargeCumulativeCountsButBoundsLiveCountsAndText() {
        JsonObject status = status(); status.getAsJsonObject("particles").addProperty("requested",9_007_199_254_740_991L);
        assertEquals(9_007_199_254_740_991L,ParticleDiagnostics.parse(status).requested());
        for(String field:new String[]{"reason","lastReason","lastType","lastBlock"}) {
            status=status(); status.getAsJsonObject("particles").addProperty(field,"\n/path/to/private/asset");
            assertFalse(ParticleDiagnostics.parse(status).supported());
            status=status(); status.getAsJsonObject("particles").addProperty(field,"x".repeat(129));
            assertFalse(ParticleDiagnostics.parse(status).supported());
        }
        status=status(); status.add("particles",new com.google.gson.JsonArray());
        assertFalse(ParticleDiagnostics.parse(status).supported());
    }
    @Test public void earlierDiagnosticExtensionCanOmitPeakInstances() {
        JsonObject status = status(); status.getAsJsonObject("particles").remove("peakInstances");
        var parsed = ParticleDiagnostics.parse(status);
        assertTrue(parsed.supported()); assertEquals(0,parsed.peakInstances());
    }
}

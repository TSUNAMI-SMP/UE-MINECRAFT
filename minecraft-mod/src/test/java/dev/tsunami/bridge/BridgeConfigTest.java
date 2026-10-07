package dev.tsunami.bridge;

import org.junit.Test;
import org.junit.Rule;
import org.junit.rules.TemporaryFolder;
import java.nio.file.Files;
import static org.junit.Assert.*;

public class BridgeConfigTest {
    @Rule public TemporaryFolder temp = new TemporaryFolder();
    @Test public void settingsRoundTripAndMissingFieldsUseDefaults() throws Exception {
        var path = temp.getRoot().toPath().resolve("settings.json");
        BridgeConfig c = BridgeConfig.load(path); assertEquals(7779, c.port);
        c.port = 7780; c.inputHz = 60; c.bowEvents = true; c.videoQuality=3;c.videoSkipVanilla=false;
        c.lighting=false;c.vanillaSky=true;c.particleScale=.5;c.particleDensity=.5;c.particleLifetime=1.25;
        c.terrainDistanceChunks=6;c.worldRadius=12;c.worldHalfHeight=6;c.targetFps=45;c.videoTransport=2;c.save(path);
        var loaded = BridgeConfig.load(path); assertEquals(7780, loaded.port); assertEquals(60, loaded.inputHz); assertTrue(loaded.bowEvents);assertEquals(3,loaded.videoQuality);assertFalse(loaded.videoSkipVanilla);
        assertFalse(loaded.lighting);assertTrue(loaded.vanillaSky);assertEquals(.5,loaded.particleScale,0);assertEquals(.5,loaded.particleDensity,0);assertEquals(1.25,loaded.particleLifetime,0);
        assertEquals(6,loaded.terrainDistanceChunks);assertEquals(12,loaded.worldRadius);assertEquals(6,loaded.worldHalfHeight);assertEquals(45,loaded.targetFps);assertEquals(2,loaded.videoTransport);
        Files.writeString(path, "{\"port\":7781}"); loaded = BridgeConfig.load(path);
        assertEquals(7781, loaded.port); assertEquals(120, loaded.inputHz); assertTrue(loaded.enabled);
        assertEquals(4,loaded.terrainDistanceChunks);assertEquals(30,loaded.targetFps);assertEquals(0,loaded.videoTransport);assertEquals(3,loaded.videoQuality);
    }
    @Test public void invalidFileIsNotReplacedOrSilentlyAccepted() throws Exception {
        var path = temp.getRoot().toPath().resolve("settings.json");
        for (String text : new String[]{"null", "[]", "{broken", "{\"port\":0}", "{\"port\":7779.5}", "{\"port\":\"7779\"}",
                "{\"enabled\":\"false\"}", "{\"enabled\":null}", "{\"inputHz\":10000}", "{\"previewRadius\":999}",
                "{\"worldSync\":\"true\"}", "{\"worldRadius\":13}", "{\"worldHalfHeight\":0}", "{\"worldHalfHeight\":7}", "{\"videoMode\":3}", "{\"videoPort\":0}",
                "{\"videoQuality\":4}", "{\"videoExposure\":7}", "{\"videoExposure\":\"1\"}", "{\"videoExposure\":null}",
                "{\"terrainDistanceChunks\":3}","{\"terrainDistanceChunks\":7}","{\"terrainDistanceChunks\":4.5}",
                "{\"targetFps\":0}","{\"targetFps\":61}","{\"targetFps\":\"30\"}","{\"videoTransport\":3}","{\"videoTransport\":true}",
                "{\"lighting\":\"false\"}","{\"vanillaSky\":true}","{\"particleScale\":0}","{\"particleDensity\":2}",
                "{\"particleLifetime\":null}","{\"particleLifetime\":3}"}) {
            Files.writeString(path, text);
            try { BridgeConfig.load(path); fail("Invalid config accepted: " + text); } catch (java.io.IOException expected) { }
            assertEquals(text, Files.readString(path));
        }
    }
    @Test public void invalidSavePreservesExistingSettings() throws Exception {
        var path = temp.getRoot().toPath().resolve("settings.json");
        BridgeConfig c = new BridgeConfig(); c.save(path); String before = Files.readString(path);
        c.port = 1;
        try { c.save(path); fail("Invalid port saved"); } catch (IllegalArgumentException expected) { }
        assertEquals(before, Files.readString(path));
    }
}

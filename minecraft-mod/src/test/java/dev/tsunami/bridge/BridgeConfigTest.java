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
        c.port = 7780; c.inputHz = 60; c.bowEvents = true; c.save(path);
        var loaded = BridgeConfig.load(path); assertEquals(7780, loaded.port); assertEquals(60, loaded.inputHz); assertTrue(loaded.bowEvents);
        Files.writeString(path, "{\"port\":7781}"); loaded = BridgeConfig.load(path);
        assertEquals(7781, loaded.port); assertEquals(120, loaded.inputHz); assertTrue(loaded.enabled);
    }
    @Test public void invalidFileIsNotReplacedOrSilentlyAccepted() throws Exception {
        var path = temp.getRoot().toPath().resolve("settings.json");
        for (String text : new String[]{"null", "[]", "{broken", "{\"port\":0}", "{\"port\":7779.5}", "{\"port\":\"7779\"}",
                "{\"enabled\":\"false\"}", "{\"enabled\":null}", "{\"inputHz\":10000}", "{\"previewRadius\":999}",
                "{\"worldSync\":\"true\"}", "{\"worldRadius\":4}", "{\"worldHalfHeight\":0}", "{\"videoMode\":3}", "{\"videoPort\":0}"}) {
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

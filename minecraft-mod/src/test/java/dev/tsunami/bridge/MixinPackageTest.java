package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.junit.Test;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import static org.junit.Assert.*;

/** Regression for the real Fabric IllegalClassLoadError reported during 0.2.0 startup. */
public class MixinPackageTest {
    private JsonObject resource(String name) throws Exception {
        var stream = getClass().getClassLoader().getResourceAsStream(name);
        assertNotNull(name, stream);
        try (var reader = new InputStreamReader(stream, StandardCharsets.UTF_8)) {
            return JsonParser.parseReader(reader).getAsJsonObject();
        }
    }
    @Test public void entrypointAndHelpersAreOutsideReservedMixinPackage() throws Exception {
        JsonObject mixins = resource("minecraft-ue-bridge.client.mixins.json");
        String prefix = mixins.get("package").getAsString() + ".";
        JsonObject mod = resource("fabric.mod.json");
        for (var entry : mod.getAsJsonObject("entrypoints").getAsJsonArray("client")) {
            assertFalse("Fabric prohibits direct class loads within a reserved Mixin package", entry.getAsString().startsWith(prefix));
        }
        for (String helper : new String[]{"BridgeConfig", "BridgeTransport", "IgnitionTracker", "BlockSnapshot"}) {
            assertFalse(("dev.tsunami.bridge." + helper).startsWith(prefix));
        }
        for (var entry : mixins.getAsJsonArray("client")) {
            String classFile = (prefix + entry.getAsString()).replace('.', '/') + ".class";
            assertNotNull("Configured Mixin class missing: " + classFile, getClass().getClassLoader().getResource(classFile));
        }
    }
}

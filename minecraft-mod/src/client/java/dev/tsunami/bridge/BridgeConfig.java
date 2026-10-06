package dev.tsunami.bridge;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonParser;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

/** No host override: traffic always stays on loopback. */
public final class BridgeConfig {
    public boolean enabled = true;
    public int port = 7779;
    public int inputHz = 120;
    public boolean notifications = true;
    public boolean bowEvents = false;
    public int previewRadius = 6;
    public int previewHalfHeight = 4;
    private static final Gson JSON = new GsonBuilder().setPrettyPrinting().create();
    public BridgeConfig copy() { return JSON.fromJson(JSON.toJson(this), BridgeConfig.class); }
    public void validate() {
        if (port < 1024 || port > 65535) throw new IllegalArgumentException("port must be 1024..65535");
        if (inputHz < 20 || inputHz > 240) throw new IllegalArgumentException("inputHz must be 20..240");
        if (previewRadius < 1 || previewRadius > 8) throw new IllegalArgumentException("previewRadius must be 1..8");
        if (previewHalfHeight < 1 || previewHalfHeight > 4) throw new IllegalArgumentException("previewHalfHeight must be 1..4");
    }
    public static BridgeConfig load(Path path) throws IOException {
        if (!Files.exists(path)) return new BridgeConfig();
        try {
            var tree = JsonParser.parseString(Files.readString(path));
            if (!tree.isJsonObject()) throw new IllegalArgumentException("config must be an object");
            var object = tree.getAsJsonObject();
            for (String name : new String[]{"port", "inputHz", "previewRadius", "previewHalfHeight"}) if (object.has(name)) {
                var value = object.get(name);
                if (!value.isJsonPrimitive() || !value.getAsJsonPrimitive().isNumber()) throw new IllegalArgumentException(name + " must be an integer");
                double number = value.getAsDouble();
                if (!Double.isFinite(number) || Math.rint(number) != number || number < Integer.MIN_VALUE || number > Integer.MAX_VALUE)
                    throw new IllegalArgumentException(name + " must be an integer");
            }
            for (String name : new String[]{"enabled", "notifications", "bowEvents"}) if (object.has(name)) {
                var value = object.get(name);
                if (!value.isJsonPrimitive() || !value.getAsJsonPrimitive().isBoolean()) throw new IllegalArgumentException(name + " must be true/false");
            }
            BridgeConfig config = JSON.fromJson(object, BridgeConfig.class);
            if (config == null) throw new IllegalArgumentException("config must be an object");
            config.validate(); return config;
        } catch (RuntimeException e) { throw new IOException("Invalid bridge config (original file retained): " + e.getMessage(), e); }
    }
    public void save(Path path) throws IOException {
        validate(); Files.createDirectories(path.toAbsolutePath().getParent());
        Path temporary = Files.createTempFile(path.toAbsolutePath().getParent(), "ue-bridge-", ".tmp");
        try {
            Files.writeString(temporary, JSON.toJson(this) + "\n");
            try { Files.move(temporary, path, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE); }
            catch (java.nio.file.AtomicMoveNotSupportedException e) { Files.move(temporary, path, StandardCopyOption.REPLACE_EXISTING); }
        } finally { Files.deleteIfExists(temporary); }
    }
}

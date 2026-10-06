package dev.tsunami.bridge;

import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;
import java.awt.image.BufferedImage;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.time.LocalDateTime;
import java.util.HexFormat;
import java.util.UUID;
import javax.imageio.ImageIO;

/** Bounded local export, independent of game classes so skin layout and manifests can be verified. */
public final class PlayerSkinData {
    private PlayerSkinData() { }

    public static BufferedImage normalized(int width, int height, int[] argb) throws IOException {
        if (width != 64 || (height != 32 && height != 64) || argb == null || argb.length != width * height)
            throw new IOException("Player skin must be 64x64 or legacy 64x32");
        BufferedImage result = new BufferedImage(64, 64, BufferedImage.TYPE_INT_ARGB);
        result.setRGB(0, 0, 64, height, argb, 0, 64);
        if (height == 32) {
            // Vanilla legacy layout: mirror the right leg and right arm into left limb faces.
            mirror(result, 4, 16, 16, 32, 4, 4); mirror(result, 8, 16, 16, 32, 4, 4);
            mirror(result, 0, 20, 24, 32, 4, 12); mirror(result, 4, 20, 16, 32, 4, 12);
            mirror(result, 8, 20, 8, 32, 4, 12); mirror(result, 12, 20, 16, 32, 4, 12);
            mirror(result, 44, 16, -8, 32, 4, 4); mirror(result, 48, 16, -8, 32, 4, 4);
            mirror(result, 40, 20, 0, 32, 4, 12); mirror(result, 44, 20, -8, 32, 4, 12);
            mirror(result, 48, 20, -16, 32, 4, 12); mirror(result, 52, 20, -8, 32, 4, 12);
            boolean opaqueLegacy = true;
            for (int y = 0; y < 32 && opaqueLegacy; y++) for (int x = 32; x < 64; x++)
                if ((result.getRGB(x, y) >>> 24) < 128) { opaqueLegacy = false; break; }
            if (opaqueLegacy) for (int y = 0; y < 32; y++) for (int x = 32; x < 64; x++)
                result.setRGB(x, y, result.getRGB(x, y) & 0x00ffffff);
        }
        opaque(result, 0, 0, 32, 16);
        opaque(result, 0, 16, 64, 32);
        opaque(result, 16, 48, 48, 64);
        return result;
    }

    private static void mirror(BufferedImage image, int x, int y, int dx, int dy, int width, int height) {
        int[] pixels = image.getRGB(x, y, width, height, null, 0, width);
        for (int py = 0; py < height; py++) for (int px = 0; px < width; px++)
            image.setRGB(x + dx + px, y + dy + py, pixels[py * width + width - 1 - px]);
    }
    private static void opaque(BufferedImage image, int left, int top, int right, int bottom) {
        for (int y = top; y < bottom; y++) for (int x = left; x < right; x++)
            image.setRGB(x, y, image.getRGB(x, y) | 0xff000000);
    }

    public static Path write(Path root, BufferedImage skin, UUID playerId, String name, boolean slim) throws IOException {
        if (skin == null || skin.getWidth() != 64 || skin.getHeight() != 64 || playerId == null)
            throw new IOException("Invalid player skin export");
        Files.createDirectories(root);
        Path directory = root.resolve("player-" + LocalDateTime.now().toString().replace(':', '-')
            + "-" + UUID.randomUUID().toString().substring(0, 8));
        Files.createDirectory(directory);
        ByteArrayOutputStream encoded = new ByteArrayOutputStream();
        if (!ImageIO.write(skin, "png", encoded)) throw new IOException("PNG encoder unavailable");
        byte[] png = encoded.toByteArray();
        Files.write(directory.resolve("skin.png"), png, StandardOpenOption.CREATE_NEW);
        JsonObject manifest = new JsonObject();
        manifest.addProperty("kind", "player"); manifest.addProperty("version", 1);
        manifest.addProperty("generatedAt", java.time.Instant.now().toString());
        JsonObject metadata = new JsonObject();
        metadata.addProperty("file", "skin.png"); metadata.addProperty("width", 64); metadata.addProperty("height", 64);
        metadata.addProperty("model", slim ? "slim" : "classic");
        try { metadata.addProperty("sha256", HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(png))); }
        catch (NoSuchAlgorithmException e) { throw new IOException("SHA-256 unavailable", e); }
        manifest.add("skin", metadata);
        JsonObject player = new JsonObject(); player.addProperty("uuid", playerId.toString());
        player.addProperty("name", name == null ? "" : name); manifest.add("player", player);
        Path destination = directory.resolve("manifest.json");
        Files.writeString(destination, new GsonBuilder().setPrettyPrinting().create().toJson(manifest),
            StandardCharsets.UTF_8, StandardOpenOption.CREATE_NEW);
        return destination;
    }
}

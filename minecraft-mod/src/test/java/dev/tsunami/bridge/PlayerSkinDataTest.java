package dev.tsunami.bridge;

import com.google.gson.JsonParser;
import java.io.IOException;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.HexFormat;
import java.util.UUID;
import java.security.MessageDigest;
import javax.imageio.ImageIO;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import static org.junit.Assert.*;

public class PlayerSkinDataTest {
    @Rule public TemporaryFolder temp = new TemporaryFolder();

    @Test public void preservesSleeveAlphaAndMakesBaseSkinOpaque() throws Exception {
        int[] skin = new int[64 * 64];
        skin[20 * 64 + 45] = 0x00654321; // Base right arm.
        skin[36 * 64 + 45] = 0x80654321; // Translucent outer sleeve.
        var image = PlayerSkinData.normalized(64, 64, skin);
        assertEquals(0xff654321, image.getRGB(45, 20));
        assertEquals(0x80654321, image.getRGB(45, 36));
        assertEquals(0, image.getRGB(0, 48));
    }

    @Test public void legacySkinsMirrorLimbsAndKeepNewOverlaysEmpty() throws Exception {
        int[] skin = new int[64 * 32]; Arrays.fill(skin, 0xffaabbcc);
        for (int x = 4; x < 8; x++) skin[20 * 64 + x] = 0xff000000 | x;
        var image = PlayerSkinData.normalized(64, 32, skin);
        assertEquals(0xff000007, image.getRGB(20, 52));
        assertEquals(0xff000004, image.getRGB(23, 52));
        assertEquals(0x00aabbcc, image.getRGB(40, 8));
        assertEquals(0, image.getRGB(48, 48));
    }

    @Test public void manifestMatchesSkinBytesAndDoesNotReplaceEarlierExport() throws Exception {
        int[] skin = new int[64 * 64]; skin[0] = 0xff102030;
        var image = PlayerSkinData.normalized(64, 64, skin);
        UUID player = UUID.randomUUID();
        var first = PlayerSkinData.write(temp.getRoot().toPath(), image, player, "Player", true);
        var second = PlayerSkinData.write(temp.getRoot().toPath(), image, player, "Player", false);
        assertNotEquals(first.getParent(), second.getParent());
        var json = JsonParser.parseString(Files.readString(first)).getAsJsonObject();
        assertEquals("player", json.get("kind").getAsString());
        assertEquals("slim", json.getAsJsonObject("skin").get("model").getAsString());
        assertEquals(player.toString(), json.getAsJsonObject("player").get("uuid").getAsString());
        var pngPath = first.getParent().resolve("skin.png");
        assertEquals(HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(pngPath))),
            json.getAsJsonObject("skin").get("sha256").getAsString());
        assertEquals(0xff102030, ImageIO.read(pngPath.toFile()).getRGB(0, 0));
    }

    @Test public void rejectsMalformedOrOversizedSkinLayouts() throws Exception {
        for (int[] size : new int[][] {{128, 128}, {64, 63}, {0, 0}}) {
            try { PlayerSkinData.normalized(size[0], size[1], new int[Math.max(0, size[0] * size[1])]); fail(); }
            catch (IOException expected) { }
        }
        try { PlayerSkinData.normalized(64, 64, new int[3]); fail(); }
        catch (IOException expected) { }
    }
}

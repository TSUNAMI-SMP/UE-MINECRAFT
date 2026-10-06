package dev.tsunami.bridge;

import java.io.IOException;
import java.nio.file.Path;
import java.util.function.Consumer;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.texture.NativeImage;
import net.minecraft.client.texture.NativeImageBackedTexture;
import net.minecraft.entity.player.PlayerSkinType;

/** Export the already loaded skin without another account request, world mutation or GPU stall. */
public final class PlayerSkinExport {
    private PlayerSkinExport() { }

    public static void export(MinecraftClient client, Path exportRoot, Consumer<String> complete) {
        try {
            Path manifest = export(client, exportRoot);
            complete.accept("プレイヤースキン書き出し完了: " + manifest.toString().replace('\\','/'));
        } catch (IOException | RuntimeException error) {
            complete.accept("プレイヤースキン書き出し失敗: " + error.getMessage());
        }
    }

    public static Path export(MinecraftClient client, Path exportRoot) throws IOException {
        if (client.player == null) throw new IOException("Enter a world before exporting your skin");
        if (!client.isOnThread()) throw new IOException("Export player skin on the Minecraft client thread");
        var textures = client.player.getSkin();
        var identifier = textures.body().texturePath();
        var loaded = client.getTextureManager().getTexture(identifier);
        int[] pixels; int width; int height;
        if (loaded instanceof NativeImageBackedTexture dynamic && dynamic.getImage() != null) {
            NativeImage image = dynamic.getImage();
            width = image.getWidth(); height = image.getHeight(); pixels = image.copyPixelsArgb();
        } else {
            // The default offline skin is a pack resource rather than a downloaded dynamic texture.
            var resource = client.getResourceManager().getResource(identifier)
                .orElseThrow(() -> new IOException("Active skin is not loaded; wait a moment and retry"));
            try (var stream = resource.getInputStream(); NativeImage image = NativeImage.read(stream)) {
                width = image.getWidth(); height = image.getHeight(); pixels = image.copyPixelsArgb();
            }
        }
        return PlayerSkinData.write(exportRoot, PlayerSkinData.normalized(width, height, pixels),
            client.player.getUuid(), client.player.getName().getString(), textures.model() == PlayerSkinType.SLIM);
    }
}

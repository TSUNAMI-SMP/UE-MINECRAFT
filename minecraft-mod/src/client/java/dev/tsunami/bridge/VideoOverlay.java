package dev.tsunami.bridge;

import net.fabricmc.fabric.api.client.rendering.v1.hud.HudElementRegistry;
import net.fabricmc.fabric.api.client.rendering.v1.hud.VanillaHudElements;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.DrawContext;
import net.minecraft.client.gl.RenderPipelines;
import net.minecraft.client.texture.NativeImage;
import net.minecraft.client.texture.NativeImageBackedTexture;
import net.minecraft.util.Identifier;

/** HUD layer preserves Minecraft input, chat, hotbar and debug information. */
public final class VideoOverlay {
    private static final Identifier TEXTURE = Identifier.of("minecraft_ue_bridge", "ue_video");
    private NativeImageBackedTexture texture;
    private VideoClient client;
    private int width, height, mode;
    public void register() {
        HudElementRegistry.attachElementBefore(VanillaHudElements.CROSSHAIR, TEXTURE, (context,ticks) -> draw(context));
    }
    public void setClient(VideoClient next, int mode) {
        this.mode = mode;
        if (client != next) { client = next; release(); }
    }
    private void release() {
        if (texture != null) MinecraftClient.getInstance().getTextureManager().destroyTexture(TEXTURE);
        texture = null;
    }
    private void draw(DrawContext context) {
        MinecraftClient mc = MinecraftClient.getInstance();
        if (mode == 0 || client == null || mc.world == null) return;
        VideoProtocol.Frame frame = client.poll();
        if (frame != null) {
            if (texture == null || width != frame.width() || height != frame.height()) {
                release(); width = frame.width(); height = frame.height();
                texture = new NativeImageBackedTexture("UE Bridge video", width,height,false);
                mc.getTextureManager().registerTexture(TEXTURE,texture);
            }
            NativeImage image = texture.getImage();
            for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) image.setColorArgb(x,y,frame.argb()[y*width+x]);
            texture.upload();
        }
        int sw=context.getScaledWindowWidth(), sh=context.getScaledWindowHeight();
        int w = mode == 2 ? sw : Math.max(80,sw/3), h = mode == 2 ? sh : Math.max(45,w*9/16);
        int x = mode == 2 ? 0 : sw-w-8, y = mode == 2 ? 0 : 8;
        context.fill(x,y,x+w,y+h,0xff101010);
        if (texture != null && client.fresh()) {
            // Preserve aspect ratio instead of stretching the camera image.
            int dw=w, dh=w*height/width; if (dh>h) { dh=h; dw=h*width/height; }
            context.drawTexture(RenderPipelines.GUI_TEXTURED,TEXTURE,x+(w-dw)/2,y+(h-dh)/2,0,0,dw,dh,width,height,width,height);
        } else context.drawTextWithShadow(mc.textRenderer,client.status(),x+4,y+4,0xffffffff);
    }
}

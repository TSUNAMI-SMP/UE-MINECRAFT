package dev.tsunami.bridge;

import net.fabricmc.fabric.api.client.rendering.v1.hud.HudElementRegistry;
import net.fabricmc.fabric.api.client.rendering.v1.hud.VanillaHudElements;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.DrawContext;
import net.minecraft.client.gl.RenderPipelines;
import net.minecraft.client.texture.NativeImage;
import net.minecraft.client.texture.NativeImageBackedTexture;
import net.minecraft.util.Identifier;
import org.lwjgl.system.MemoryUtil;

/** HUD layer preserves Minecraft input, chat, hotbar and debug information. */
public final class VideoOverlay {
    private static final Identifier TEXTURE = Identifier.of("minecraft_ue_bridge", "ue_video");
    private NativeImageBackedTexture texture;
    private VideoClient client;
    private int width, height, mode;
    private BridgeTransport transport;
    private double inputToUpload=-1,readbackMs,encodeMs,decodeMs,uploadMs;
    private VideoProtocol.Frame uploaded;
    private final GpuVideoBridge gpu=new GpuVideoBridge();
    public void setTransport(BridgeTransport transport) { this.transport=transport; }
    public String timing() {
        return inputToUpload<0 ? "映像遅延計測待ち" : String.format(java.util.Locale.ROOT,
            "入力→描画投入=%.1fms 映像準備=%.1f 圧縮=%.1f 復号=%.1f copy/upload=%.1fms / %s",
            inputToUpload,readbackMs,encodeMs,decodeMs,uploadMs,client==null?"":client.latency());
    }
    public void register() {
        HudElementRegistry.attachElementBefore(VanillaHudElements.CROSSHAIR, TEXTURE, (context,ticks) -> draw(context));
    }
    public void setClient(VideoClient next, int mode) {
        this.mode = mode;
        if (client != next) { client = next; uploaded=null; inputToUpload=-1; release(); }
    }
    private void release() {
        gpu.close();
        if (texture != null) MinecraftClient.getInstance().getTextureManager().destroyTexture(TEXTURE);
        texture = null;
    }
    private void draw(DrawContext context) {
        MinecraftClient mc = MinecraftClient.getInstance();
        if (mode == 0 || client == null || mc.world == null) return;
        VideoProtocol.Frame frame = VanillaSkyComposite.displayed(client);
        if (frame != null && frame!=uploaded) {
            if (texture == null || width != frame.width() || height != frame.height()) {
                release(); width = frame.width(); height = frame.height();
                texture = new NativeImageBackedTexture("UE Bridge video", width,height,false);
                mc.getTextureManager().registerTexture(TEXTURE,texture);
            }
            long uploadStart=System.nanoTime();
            try {
                if(frame.gpu()!=null) gpu.copy(frame,texture);
                else {
                    NativeImage image = texture.getImage();
                    // NativeImage.imageId() is its native RGBA buffer in Minecraft 1.21.11.
                    MemoryUtil.memIntBuffer(image.imageId(),width*height).put(frame.abgr());texture.upload();
                }
            } catch(RuntimeException | UnsatisfiedLinkError e) {
                client.disableGpu(e.getMessage());gpu.close();client.release(frame);return;
            }
            client.release(frame);
            uploadMs=(System.nanoTime()-uploadStart)/1_000_000.0;
            readbackMs=frame.readbackMs();encodeMs=frame.encodeMs();decodeMs=frame.decodeMs();
            inputToUpload=transport==null || frame.inputSequence()==0 ? -1 : transport.inputAgeMillis(frame.inputSequence());
            uploaded=frame;
        }
        int sw=context.getScaledWindowWidth(), sh=context.getScaledWindowHeight();
        int w = mode == 2 ? sw : Math.max(80,sw/3), h = mode == 2 ? sh : Math.max(45,w*9/16);
        int x = mode == 2 ? 0 : sw-w-8, y = mode == 2 ? 0 : 8;
        boolean sky=mode==2 && VanillaSkyComposite.active() && uploaded!=null && uploaded.skyMask();
        if(!sky) context.fill(x,y,x+w,y+h,0xff101010);
        if (texture != null && client.fresh()) {
            // Preserve aspect ratio instead of stretching the camera image.
            int dw=w, dh=w*height/width; if (dh>h) { dh=h; dw=h*width/height; }
            if(sky) {
                int left=x+(w-dw)/2,top=y+(h-dh)/2;
                context.fill(x,y,x+w,top,0xff101010);context.fill(x,top+dh,x+w,y+h,0xff101010);
                context.fill(x,top,left,top+dh,0xff101010);context.fill(left+dw,top,x+w,top+dh,0xff101010);
            }
            context.drawTexture(RenderPipelines.GUI_TEXTURED,TEXTURE,x+(w-dw)/2,y+(h-dh)/2,0,0,dw,dh,width,height,width,height);
            if(uploaded!=null) client.displayed(uploaded,inputToUpload);
        } else context.drawTextWithShadow(mc.textRenderer,client.status(),x+4,y+4,0xffffffff);
        if(mode==2 && transport!=null && transport.diagnostics().ueControl() && transport.mobsSupported()) {
            double health=transport.playerHealth();
            String label=health<=0 ? "UE体力: 0/20  /uebridge respawn で復活" : String.format(java.util.Locale.ROOT,"UE体力: %.0f/20",health);
            context.drawTextWithShadow(mc.textRenderer,label,8,sh-22,health<=0 ? 0xffff5555 : 0xffffffff);
        }
    }
}

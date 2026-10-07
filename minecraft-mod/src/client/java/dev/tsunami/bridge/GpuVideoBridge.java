package dev.tsunami.bridge;

import net.minecraft.client.texture.GlTexture;
import net.minecraft.client.texture.NativeImageBackedTexture;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;

/** Windows/NVIDIA shared frame receiver. OpenGL work always stays on Minecraft's render thread. */
public final class GpuVideoBridge implements AutoCloseable {
    private static boolean checked,available;
    private static String diagnostic="GPU共有未確認";
    private long context,adapter;
    private int generation;
    public static synchronized boolean available() {
        if(checked) return available;
        checked=true;
        if(!System.getProperty("os.name","").toLowerCase(Locale.ROOT).contains("windows")
            || !System.getProperty("os.arch","").equals("amd64")) {diagnostic="GPU共有はWindows x64/NVIDIA用";return false;}
        if(Boolean.getBoolean("uebridge.video.jpeg")){diagnostic="JPEGを指定";return false;}
        try(var resource=GpuVideoBridge.class.getResourceAsStream("/native/win64/uebridge_gpu.dll")) {
            if(resource==null) throw new IOException("同梱GPU DLLがありません");
            byte[] bytes=resource.readNBytes(8*1024*1024+1);
            if(bytes.length<1024 || bytes.length>8*1024*1024) throw new IOException("GPU DLLサイズ不正");
            Path directory=Files.createTempDirectory("uebridge-gpu-");Path dll=directory.resolve("uebridge_gpu.dll");
            Files.write(dll,bytes);dll.toFile().deleteOnExit();directory.toFile().deleteOnExit();System.load(dll.toAbsolutePath().toString());
            available=probe();diagnostic=available ? "GPU共有利用可能（UEはD3D11が必要）" : error();
        } catch(IOException | UnsatisfiedLinkError | RuntimeException e) {diagnostic="GPU共有不可: "+e.getMessage();}
        return available;
    }
    public static String diagnostic(){return diagnostic;}
    public void copy(VideoProtocol.Frame frame,NativeImageBackedTexture target) {
        var gpu=frame.gpu();if(gpu==null) throw new IllegalArgumentException("Not a GPU frame");
        if(!(target.getGlTexture() instanceof GlTexture gl)) throw new IllegalStateException("MinecraftのOpenGLテクスチャが必要です");
        if(context==0 || adapter!=gpu.adapter() || generation!=gpu.generation()) {
            close();context=open(gpu.adapter());adapter=gpu.adapter();generation=gpu.generation();
        }
        copy(context,gpu.handle(),gl.getGlId(),frame.width(),frame.height());
    }
    @Override public void close(){if(context!=0){close(context);context=0;}}
    private static native boolean probe();
    private static native String error();
    private static native long open(long adapter);
    private static native void copy(long context,long handle,int texture,int width,int height);
    private static native void close(long context);
}

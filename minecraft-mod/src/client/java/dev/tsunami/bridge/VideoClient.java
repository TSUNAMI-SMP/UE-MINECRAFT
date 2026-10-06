package dev.tsunami.bridge;

import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicReference;

/** Dedicated daemon: socket read/JPEG decoding stay away from Minecraft's game/render thread. */
public final class VideoClient implements AutoCloseable {
    private final AtomicReference<VideoProtocol.Frame> latest = new AtomicReference<>();
    private final Thread worker;
    private volatile boolean stopped;
    private volatile Socket socket;
    private volatile long lastFrame;
    private volatile String message = "UE映像へ接続中";
    private volatile int width,height;
    private volatile double fps;
    public VideoClient(int port, String session) {
        worker = new Thread(() -> run(port,session), "UE-Bridge-Video"); worker.setDaemon(true); worker.start();
    }
    private void run(int port, String session) {
        while (!stopped) {
            try (Socket connection = new Socket()) {
                socket = connection; if (stopped) break;
                connection.connect(new InetSocketAddress("127.0.0.1",port),1000);
                connection.setReceiveBufferSize(64*1024);connection.setSoTimeout(2000); connection.setTcpNoDelay(true);
                connection.getOutputStream().write(("UEBH"+session).getBytes(StandardCharsets.US_ASCII));
                DataInputStream input = new DataInputStream(new BufferedInputStream(connection.getInputStream()));
                long windowStart=System.nanoTime(); int frames=0;
                while (!stopped) {
                    VideoProtocol.Frame frame = VideoProtocol.read(input);
                    latest.set(frame); lastFrame = System.nanoTime(); message = "UE映像受信中";
                    width=frame.width(); height=frame.height(); frames++;
                    if(lastFrame-windowStart>=1_000_000_000L) { fps=frames*1_000_000_000.0/(lastFrame-windowStart); frames=0; windowStart=lastFrame; }
                }
            } catch (IOException | RuntimeException e) {
                message = "UE映像待ち: " + e.getClass().getSimpleName();
            } finally { socket = null; }
            if (!stopped) try { Thread.sleep(1000); } catch (InterruptedException ignored) { break; }
        }
        latest.set(null);
    }
    public VideoProtocol.Frame poll() { return latest.getAndSet(null); }
    public boolean fresh() { return lastFrame != 0 && System.nanoTime()-lastFrame < 1_000_000_000L; }
    public String status() { return fresh() ? String.format(java.util.Locale.ROOT,"UE映像 %dx%d / %.1ffps",width,height,fps) : message; }
    @Override public void close() {
        stopped = true; Socket s = socket; if (s != null) try { s.close(); } catch (IOException ignored) { }
        worker.interrupt(); latest.set(null);
    }
}

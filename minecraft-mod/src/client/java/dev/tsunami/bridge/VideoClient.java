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
    private volatile DataOutputStream acknowledgements;
    private volatile long lastFrame;
    private volatile String message = "UE映像へ接続中";
    private volatile int width,height;
    private final VideoTiming timing=new VideoTiming();
    private volatile String backend="JPEG";
    private volatile boolean gpuAllowed;
    private volatile String gpuDiagnostic="";
    public VideoClient(int port, String session) {
        this(port,session,false);
    }
    public VideoClient(int port,String session,boolean maskCapable) {
        this(port,session,maskCapable,0);
    }
    public VideoClient(int port,String session,boolean maskCapable,int transportMode) {
        this(port,session,maskCapable,transportMode!=1 && maskCapable && GpuVideoBridge.available());
        gpuDiagnostic=transportMode==1 ? "JPEG指定" : GpuVideoBridge.diagnostic();
    }
    /** Explicit capability injection keeps wire/lease tests independent of a Windows GPU. */
    VideoClient(int port,String session,boolean maskCapable,boolean gpuCapable) {
        gpuAllowed=gpuCapable;
        worker = new Thread(() -> run(port,session,maskCapable), "UE-Bridge-Video"); worker.setDaemon(true); worker.start();
    }
    private void run(int port, String session,boolean maskCapable) {
        while (!stopped) {
            try (Socket connection = new Socket()) {
                socket = connection; if (stopped) break;
                connection.connect(new InetSocketAddress("127.0.0.1",port),1000);
                connection.setReceiveBufferSize(256*1024);connection.setSoTimeout(2000); connection.setTcpNoDelay(true);
                acknowledgements=new DataOutputStream(connection.getOutputStream());
                acknowledgements.write(((gpuAllowed ? "UEB5" : maskCapable ? "UEB3" : "UEBH")+session).getBytes(StandardCharsets.US_ASCII));
                DataInputStream input = new DataInputStream(new BufferedInputStream(connection.getInputStream()));
                while (!stopped) {
                    VideoProtocol.Frame frame = VideoProtocol.read(input);
                    release(latest.getAndSet(frame));lastFrame = System.nanoTime();message="UE映像受信中";
                    backend=frame.gpu()==null ? "JPEG" : "GPU共有";
                    width=frame.width();height=frame.height();timing.received(lastFrame);
                }
            } catch (IOException | RuntimeException e) {
                message = "UE映像待ち: " + e.getClass().getSimpleName();
            } finally { socket = null;acknowledgements=null;latest.set(null); }
            if (!stopped) try { Thread.sleep(1000); } catch (InterruptedException ignored) { break; }
        }
        latest.set(null);
    }
    public VideoProtocol.Frame poll() { return latest.getAndSet(null); }
    public void release(VideoProtocol.Frame frame) {
        if(frame==null || frame.gpu()==null) return;
        var gpu=frame.gpu();sendAcknowledgement(0x55454241,frame.sequence(),gpu.slot(),gpu.generation()); // UEBA
    }
    private synchronized void sendAcknowledgement(int magic,long sequence,int slot,int generation) {
        DataOutputStream output=acknowledgements;if(output==null || stopped) return;
        try {output.writeInt(magic);output.writeInt((int)sequence);output.writeInt(slot);output.writeInt(generation);output.flush();}
        catch(IOException ignored){Socket s=socket;if(s!=null)try{s.close();}catch(IOException ignoredClose){}}
    }
    public void disableGpu(String reason) {
        gpuAllowed=false;gpuDiagnostic="GPU共有失敗→JPEG: "+reason;message=gpuDiagnostic;
        sendAcknowledgement(0x55454246,0,0,0); // UEBF: return to JPEG without replacing the input session.
    }
    public void displayed(VideoProtocol.Frame frame,double inputAge) {timing.displayed(frame.sequence(),System.nanoTime(),inputAge);}
    public String latency() {
        double p50=timing.percentile(.5),p95=timing.percentile(.95);
        return p50<0 ? "入力→描画投入 計測待ち" : String.format(java.util.Locale.ROOT,"入力→描画投入 p50=%.1f p95=%.1fms",p50,p95);
    }
    public String gpuDiagnostic(){return gpuDiagnostic;}
    public boolean fresh() { return lastFrame != 0 && System.nanoTime()-lastFrame < 1_000_000_000L; }
    public String status() {long now=System.nanoTime();return fresh() ? String.format(java.util.Locale.ROOT,
        "UE映像 %dx%d %s / 受信%.1f 描画投入%.1ffps / %s",width,height,backend,timing.receivedFps(now),timing.displayedFps(now),latency()) : message;}
    @Override public void close() {
        stopped = true; Socket s = socket; if (s != null) try { s.close(); } catch (IOException ignored) { }
        worker.interrupt(); latest.set(null);
    }
}

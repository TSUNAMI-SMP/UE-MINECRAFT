package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.PortUnreachableException;
import java.nio.ByteBuffer;
import java.nio.channels.DatagramChannel;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.ArrayDeque;
import java.util.UUID;
import java.util.function.LongSupplier;

/** Fabric-independent, bounded, nonblocking loopback transport. Game-thread owned. */
public final class BridgeTransport implements AutoCloseable {
    public static final int MAX_PACKET_BYTES = 2048;
    public static final int MAX_PENDING_EVENTS = 64;
    private final DatagramChannel channel;
    private final LongSupplier clock;
    private final String session = UUID.randomUUID().toString();
    private final ByteBuffer receive = ByteBuffer.allocate(MAX_PACKET_BYTES + 1);
    private final LinkedHashMap<String, Pending> pending = new LinkedHashMap<>();
    private final LinkedHashMap<Long, Long> inputTimes = new LinkedHashMap<>();
    private final ArrayDeque<JsonObject> feedback = new ArrayDeque<>();
    private final LinkedHashMap<String, Boolean> seenFeedback = new LinkedHashMap<>();
    private boolean playerVisualsSupported, feedbackSupported;
    private boolean videoV3, blockModelsSupported, mobsSupported, skySupported;
    private boolean blockPaletteReady, mobPaletteReady;
    public boolean blockPaletteReady() {return diagnostics().connected() && blockPaletteReady;}
    public boolean mobPaletteReady() {return diagnostics().connected() && mobPaletteReady;}
    private boolean lightingEnabled=true, vanillaSkyEnabled;
    private int mobCount, mobMissing;
    private int maskPixels,maskForeground,maskTranslucent;
    private String blockModelError="";
    private double playerHealth=20;
    public double playerHealth() {return diagnostics().connected() ? playerHealth : 20;}
    public boolean videoV3Supported() { return diagnostics().connected() && videoV3; }
    public boolean blockModelsSupported() { return diagnostics().connected() && blockModelsSupported; }
    public boolean mobsSupported() { return diagnostics().connected() && mobsSupported; }
    public boolean skySupported() { return diagnostics().connected() && skySupported; }
    public String renderStatus() { return "照明="+lightingEnabled+" MC空="+vanillaSkyEnabled+" マスク="+maskForeground+"/"+maskPixels+" 半透明="+maskTranslucent; }
    public String blockModelError() {return blockModelError;}
    public String mobStatus() { return "モブ="+mobCount+" 素材不足="+mobMissing; }
    private ParticleDiagnostics particles = ParticleDiagnostics.unavailable("unsupported");
    public ParticleDiagnostics particleDiagnostics() {
        return diagnostics().connected() ? particles : ParticleDiagnostics.unavailable("disconnected");
    }
    public boolean playerVisualsSupported() { return diagnostics().connected() && playerVisualsSupported; }
    public JsonObject pollFeedback() { return feedback.pollFirst(); }
    private long sequence, lastStatus, lastStatusSequence, sentInputs, acknowledged, expired, lastRtt;
    private boolean hasStatus, cameraReady, vfxReady;
    private int walls;
    private String receiver = "unknown";
    private String build = "unknown";
    private String receiverId = "";
    private boolean worldSupported, videoSupported;
    private boolean texturesSupported, videoControlsSupported;
    private int textureMaterials;
    private boolean authoritySupported, worldSealed, ueControl, actionsSupported, videoV2;
    private String lastAction="";
    public boolean actionsSupported() { return diagnostics().connected() && actionsSupported; }
    public boolean videoV2Supported() { return diagnostics().connected() && videoV2; }
    public String lastAction() { return lastAction; }
    public double inputAgeMillis(long seq) {
        Long sent=inputTimes.get(seq); return sent==null ? -1 : Math.max(0,clock.getAsLong()-sent)/1_000_000.0;
    }
    private String importId="";
    private int importedCells;
    public record AuthorityPose(double x,double y,double z,boolean grounded) {}
    private AuthorityPose pose;
    private long poseSequence, poseTime;
    public AuthorityPose authorityPose() { return pose!=null && clock.getAsLong()-poseTime<=1_000_000_000L ? pose : null; }
    private record Pending(byte[] bytes, long created, long sent, boolean attempted) {}
    public record Diagnostics(boolean connected, boolean cameraReady, boolean vfxReady, int walls,
                              int pending, long sentInputs, long acknowledged, long expired, double rttMillis, String receiver,
                              String build, boolean worldSupported, boolean videoSupported, String receiverId,
                              boolean texturesSupported,boolean videoControlsSupported,int textureMaterials,boolean authoritySupported,boolean worldSealed,boolean ueControl,String importId,int importedCells) {}

    public BridgeTransport(int port) throws IOException { this(port, System::nanoTime); }
    BridgeTransport(int port, LongSupplier clock) throws IOException {
        if (port < 1024 || port > 65535) throw new IllegalArgumentException("Port must be 1024..65535");
        this.clock = clock;
        channel = DatagramChannel.open();
        try {
            channel.bind(new InetSocketAddress("127.0.0.1", 0));
            channel.connect(new InetSocketAddress("127.0.0.1", port));
            channel.configureBlocking(false);
        } catch (IOException | RuntimeException e) { channel.close(); throw e; }
    }
    public JsonObject packet(String kind) {
        if (sequence >= 9_007_199_254_740_991L) throw new IllegalStateException("Reconnect to renew sequence");
        JsonObject p = new JsonObject();
        p.addProperty("v", 1); p.addProperty("kind", kind); p.addProperty("session", session);
        p.addProperty("seq", ++sequence); return p;
    }
    private boolean send(byte[] bytes) throws IOException {
        try { return channel.write(ByteBuffer.wrap(bytes)) == bytes.length; }
        catch (PortUnreachableException ignored) { return false; }
    }
    private static byte[] encode(JsonObject p) throws IOException {
        byte[] bytes = p.toString().getBytes(StandardCharsets.UTF_8);
        if (bytes.length > MAX_PACKET_BYTES) throw new IOException("Bridge packet exceeds 2048 bytes");
        return bytes;
    }
    public void input(JsonObject p) throws IOException {
        if (send(encode(p))) {
            ++sentInputs; inputTimes.put(p.get("seq").getAsLong(), clock.getAsLong());
            if (inputTimes.size() > 256) inputTimes.remove(inputTimes.keySet().iterator().next());
        }
    }
    public void event(JsonObject p) throws IOException {
        if (pending.size() >= MAX_PENDING_EVENTS) throw new IOException("Bridge event queue is full");
        String id = UUID.randomUUID().toString(); p.addProperty("eventId", id);
        byte[] bytes = encode(p);
        pending.put(id, new Pending(bytes, clock.getAsLong(), 0, false));
        pump();
    }
    public int availableEvents() { return MAX_PENDING_EVENTS - pending.size(); }
    public String session() { return session; }
    public Diagnostics diagnostics() {
        boolean connected = hasStatus && clock.getAsLong() - lastStatus <= 1_000_000_000L;
        return new Diagnostics(connected, connected && cameraReady, connected && vfxReady, connected ? walls : 0,
                pending.size(), sentInputs, acknowledged, expired, lastRtt / 1_000_000.0, receiver,
                build, connected && worldSupported, connected && videoSupported, receiverId,
                connected && texturesSupported,connected && videoControlsSupported,connected ? textureMaterials : 0, connected && authoritySupported, connected && worldSealed, connected && ueControl, importId, importedCells);
    }
    private static boolean number(JsonObject p, String name) {
        return p.has(name) && p.get(name).isJsonPrimitive() && p.getAsJsonPrimitive(name).isNumber();
    }
    private static boolean bool(JsonObject p, String name) {
        return p.has(name) && p.get(name).isJsonPrimitive() && p.getAsJsonPrimitive(name).isBoolean();
    }
    private void reply(JsonObject p) throws IOException {
        if (!number(p, "v") || p.get("v").getAsDouble() != 1
                || !session.equals(p.get("session").getAsString())) return;
        String kind = p.get("kind").getAsString();
        if ("ack".equals(kind)) {
            if (pending.remove(p.get("eventId").getAsString()) != null) ++acknowledged;
        } else if ("feedback".equals(kind) || "mob_feedback".equals(kind)) {
            if(!feedbackSupported || !diagnostics().connected()) return;
            if("mob_feedback".equals(kind) ? !MobFeedback.valid(p) : VanillaFeedbackData.parse(p)==null) return;
            if(!receiverId.equals(p.get("receiverId").getAsString())) return;
            if(!number(p,"seq")) return;
            double feedbackSeq=p.get("seq").getAsDouble();
            if(!Double.isFinite(feedbackSeq) || feedbackSeq!=Math.rint(feedbackSeq) || feedbackSeq<1 || feedbackSeq>sequence) return;
            long seq=p.get("seq").getAsLong(); Long sent=inputTimes.get(seq);
            if(sent==null || clock.getAsLong()-sent>1_000_000_000L) return;
            String id=p.get("effectId").getAsString();
            try { if(!java.util.UUID.fromString(id).toString().equals(id.toLowerCase(java.util.Locale.ROOT))) return; } catch(IllegalArgumentException invalid) { return; }
            if(!seenFeedback.containsKey(id)) {
                if(feedback.size()>=64) return; // Sender retries; never ACK a discarded effect.
                feedback.addLast(p.deepCopy());seenFeedback.put(id,true);
                if(seenFeedback.size()>256) seenFeedback.remove(seenFeedback.keySet().iterator().next());
            }
            JsonObject ack=packet("feedback_ack");ack.addProperty("effectId",id);send(encode(ack));
        } else if("pose".equals(kind)) {
            if(!p.has("receiverId") || !receiverId.equals(p.get("receiverId").getAsString()) || !authoritySupported
                    || !number(p,"seq") || !number(p,"poseSeq") || !number(p,"x") || !number(p,"y") || !number(p,"z") || !bool(p,"grounded")) return;
            double seq=p.get("seq").getAsDouble(), revision=p.get("poseSeq").getAsDouble();
            if(!Double.isFinite(seq) || seq!=Math.rint(seq) || seq<1 || seq>sequence
                    || !inputTimes.containsKey((long)seq) || clock.getAsLong()-inputTimes.get((long)seq)>1_000_000_000L
                    || !Double.isFinite(revision) || revision!=Math.rint(revision) || revision<=poseSequence || revision>9_007_199_254_740_991L) return;
            double x=p.get("x").getAsDouble(),y=p.get("y").getAsDouble(),z=p.get("z").getAsDouble();
            if(!Double.isFinite(x) || !Double.isFinite(y) || !Double.isFinite(z) || Math.abs(x)>100000 || Math.abs(y)>100000 || Math.abs(z)>100000) return;
            pose=new AuthorityPose(x,y,z,p.get("grounded").getAsBoolean()); poseSequence=(long)revision; poseTime=clock.getAsLong();
        } else if ("status".equals(kind) && number(p, "seq") && bool(p, "cameraReady")
                && bool(p, "vfxReady") && number(p, "walls")) {
            double seq = p.get("seq").getAsDouble(), count = p.get("walls").getAsDouble();
            if (!Double.isFinite(seq) || seq != Math.rint(seq) || seq <= lastStatusSequence || seq > sequence
                    || !Double.isFinite(count) || count != Math.rint(count) || count < 0 || count > 100000) return;
            Long time = inputTimes.get((long) seq); if (time == null) return;
            lastStatus = clock.getAsLong(); lastRtt = Math.max(0, lastStatus - time);
            lastStatusSequence = (long) seq; hasStatus = true;
            cameraReady = p.get("cameraReady").getAsBoolean(); vfxReady = p.get("vfxReady").getAsBoolean(); walls = (int) count;
            receiver = p.has("receiver") && "diagnostic".equals(p.get("receiver").getAsString()) ? "diagnostic" : "ue";
            build = p.has("build") && p.get("build").isJsonPrimitive() && p.getAsJsonPrimitive("build").isString()
                    ? p.get("build").getAsString() : "unknown";
            actionsSupported=bool(p,"blockActionsV1") && p.get("blockActionsV1").getAsBoolean();
            videoV2=bool(p,"videoV2") && p.get("videoV2").getAsBoolean();
            videoV3=bool(p,"videoV3") && p.get("videoV3").getAsBoolean();
            blockModelsSupported=bool(p,"blockModelsV2") && p.get("blockModelsV2").getAsBoolean();
            blockPaletteReady=bool(p,"blockPaletteReady") && p.get("blockPaletteReady").getAsBoolean();
            mobPaletteReady=bool(p,"mobPaletteReady") && p.get("mobPaletteReady").getAsBoolean();
            mobsSupported=bool(p,"mobsV1") && p.get("mobsV1").getAsBoolean();
            skySupported=bool(p,"skySupported") && p.get("skySupported").getAsBoolean();
            lightingEnabled=!bool(p,"lightingEnabled") || p.get("lightingEnabled").getAsBoolean();
            vanillaSkyEnabled=bool(p,"vanillaSkyEnabled") && p.get("vanillaSkyEnabled").getAsBoolean();
            double mobs=number(p,"mobCount") ? p.get("mobCount").getAsDouble() : 0;
            double missing=number(p,"mobMissing") ? p.get("mobMissing").getAsDouble() : 0;
            mobCount=Double.isFinite(mobs) && mobs==Math.rint(mobs) && mobs>=0 && mobs<=128 ? (int)mobs : 0;
            mobMissing=Double.isFinite(missing) && missing==Math.rint(missing) && missing>=0 && missing<=100000 ? (int)missing : 0;
            double hp=number(p,"uePlayerHealth") ? p.get("uePlayerHealth").getAsDouble() : 20;
            playerHealth=Double.isFinite(hp) && hp>=0 && hp<=20 ? hp : 20;
            maskPixels=statusCount(p,"maskPixels",1920*1080);maskForeground=statusCount(p,"maskForeground",maskPixels);
            maskTranslucent=statusCount(p,"maskTranslucent",maskForeground);
            blockModelError=p.has("blockModelError") && p.get("blockModelError").isJsonPrimitive() && p.getAsJsonPrimitive("blockModelError").isString()
                ? p.get("blockModelError").getAsString() : "";
            if(blockModelError.length()>160) blockModelError="invalid-diagnostic";
            playerVisualsSupported=bool(p,"playerVisualsV1") && p.get("playerVisualsV1").getAsBoolean();
            feedbackSupported=bool(p,"vanillaFeedbackV1") && p.get("vanillaFeedbackV1").getAsBoolean();
            particles=ParticleDiagnostics.parse(p);
            lastAction=p.has("lastAction") && p.get("lastAction").isJsonPrimitive() && p.getAsJsonPrimitive("lastAction").isString() ? p.get("lastAction").getAsString() : "";
            authoritySupported=bool(p,"authorityV1") && p.get("authorityV1").getAsBoolean();
            worldSealed=bool(p,"worldSealed") && p.get("worldSealed").getAsBoolean();
            ueControl=bool(p,"ueControl") && p.get("ueControl").getAsBoolean();
            importId=p.has("importId") && p.get("importId").isJsonPrimitive() && p.getAsJsonPrimitive("importId").isString() ? p.get("importId").getAsString() : "";
            double cells=number(p,"importedCells") ? p.get("importedCells").getAsDouble() : 0;
            importedCells=Double.isFinite(cells) && cells==Math.rint(cells) && cells>=0 && cells<=245 ? (int)cells : 0;
            worldSupported=bool(p,"worldV1") && p.get("worldV1").getAsBoolean();
            videoSupported=bool(p,"videoV1") && p.get("videoV1").getAsBoolean();
            texturesSupported=bool(p,"blockTexturesV1") && p.get("blockTexturesV1").getAsBoolean();
            videoControlsSupported=bool(p,"videoControlsV1") && p.get("videoControlsV1").getAsBoolean();
            double materials=number(p,"textureMaterials") ? p.get("textureMaterials").getAsDouble() : 0;
            textureMaterials=Double.isFinite(materials) && materials==Math.rint(materials) && materials>=0 && materials<=4096 ? (int)materials : 0;
            String nextReceiver=p.has("receiverId") && p.get("receiverId").isJsonPrimitive() && p.getAsJsonPrimitive("receiverId").isString()
                    ? p.get("receiverId").getAsString() : "";
            if(!receiverId.equals(nextReceiver)) { pose=null; poseSequence=0; feedback.clear();seenFeedback.clear(); }
            receiverId=nextReceiver;
        }
    }
    public void pump() throws IOException {
        for (int i = 0; i < 32; i++) {
            receive.clear(); int bytes;
            try { bytes = channel.read(receive); } catch (PortUnreachableException ignored) { continue; }
            if (bytes <= 0) break;
            if (bytes > MAX_PACKET_BYTES) continue;
            receive.flip();
            try { reply(JsonParser.parseString(StandardCharsets.UTF_8.decode(receive).toString()).getAsJsonObject()); }
            catch (RuntimeException ignored) { /* Untrusted/malformed datagram. */ }
        }
        long now = clock.getAsLong();
        var it = pending.entrySet().iterator();
        while (it.hasNext()) {
            var e = it.next(); Pending p = e.getValue();
            if (now - p.created >= 2_000_000_000L) { it.remove(); ++expired; continue; }
            if (!p.attempted || now - p.sent >= 100_000_000L) {
                send(p.bytes); e.setValue(new Pending(p.bytes, p.created, now, true));
            }
        }
    }
    private static int statusCount(JsonObject p,String name,int maximum) {
        double value=number(p,name) ? p.get(name).getAsDouble() : 0;
        return Double.isFinite(value) && value==Math.rint(value) && value>=0 && value<=maximum ? (int)value : 0;
    }
    @Override public void close() throws IOException { pending.clear(); inputTimes.clear(); feedback.clear();seenFeedback.clear();channel.close(); }
}

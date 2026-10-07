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
    public static final int MAX_EVENT_RECEIPTS = 1024;
    private static final int MAX_ITEM_FEEDBACK = 64;
    private final DatagramChannel channel;
    private final LongSupplier clock;
    private final String session = UUID.randomUUID().toString();
    private final ByteBuffer receive = ByteBuffer.allocate(MAX_PACKET_BYTES + 1);
    private final LinkedHashMap<String, Pending> pending = new LinkedHashMap<>();
    private final LinkedHashMap<String, EventReceipt> eventReceipts = new LinkedHashMap<>();
    private final LinkedHashMap<Long, Long> inputTimes = new LinkedHashMap<>();
    private final ArrayDeque<JsonObject> feedback = new ArrayDeque<>();
    private final ArrayDeque<JsonObject> itemFeedback = new ArrayDeque<>();
    private final LinkedHashMap<String, Boolean> seenFeedback = new LinkedHashMap<>();
    private boolean playerVisualsSupported, feedbackSupported;
    private boolean videoV3, blockModelsSupported, mobsSupported, skySupported;
    private boolean terrainSupported, itemDropsSupported, videoGpuSupported;
    private boolean blockPaletteReady, mobPaletteReady;
    public boolean blockPaletteReady() {return diagnostics().connected() && blockPaletteReady;}
    public boolean mobPaletteReady() {return diagnostics().connected() && mobPaletteReady;}
    private boolean lightingEnabled=true, vanillaSkyEnabled;
    private boolean maskCountsAvailable=true, terrainMovementReady=true;
    private int mobCount, mobMissing;
    private int maskPixels,maskForeground,maskTranslucent;
    private String blockModelError="",heldModel="",videoColor="",mobReason="";
    private boolean flying;
    private int itemModelCount,mobTemplateCount;
    public record PerformanceDiagnostics(boolean available, double fps, double frameMs, double renderMs,
            double renderGpuMs, long faces, int renderSections, int cells, int nearCollisionCells,
            double buildMs, int streamPending, int lightPending, int itemCount,
            double videoReadyMs, long videoDropped, String videoTransport, String gpuReason) {}
    private static PerformanceDiagnostics noPerformance() {
        return new PerformanceDiagnostics(false,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,"","");
    }
    private PerformanceDiagnostics performance=noPerformance();
    private long performanceTime;
    public PerformanceDiagnostics performanceDiagnostics() {
        return diagnostics().connected() && clock.getAsLong()-performanceTime<=3_000_000_000L
                ? performance : noPerformance();
    }
    public String perfStatus() {
        PerformanceDiagnostics p=performanceDiagnostics();
        if (!p.available()) return "UE性能=未受信";
        String text="UE FPS="+metric(p.fps())+" 間隔="+metric(p.frameMs())+"ms render="+metric(p.renderMs())
                +"ms 面="+countMetric(p.faces())+" セクション="+countMetric(p.renderSections())+" セル="+countMetric(p.cells())
                +" 衝突="+countMetric(p.nearCollisionCells())+" 構築="+metric(p.buildMs())+"ms";
        if (p.renderGpuMs()>=0) text+=" GPU="+metric(p.renderGpuMs())+"ms";
        return text+" 地形待ち="+countMetric(p.streamPending())+" 光待ち="+countMetric(p.lightPending())+" 落下品="+countMetric(p.itemCount())
                +" 映像準備="+metric(p.videoReadyMs())+"ms 映像破棄="+countMetric(p.videoDropped())+" 転送="+p.videoTransport()
                +(p.gpuReason().isEmpty() ? "" : " / "+p.gpuReason());
    }
    private static String metric(double value) { return value<0 ? "?" : String.format(java.util.Locale.ROOT,"%.1f",value); }
    private static String countMetric(long value) { return value<0 ? "?" : Long.toString(value); }
    private double playerHealth=20;
    public double playerHealth() {return diagnostics().connected() ? playerHealth : 20;}
    public boolean videoV3Supported() { return diagnostics().connected() && videoV3; }
    public boolean blockModelsSupported() { return diagnostics().connected() && blockModelsSupported; }
    public boolean mobsSupported() { return diagnostics().connected() && mobsSupported; }
    public boolean skySupported() { return diagnostics().connected() && skySupported; }
    public boolean terrainSupported() { return diagnostics().connected() && terrainSupported; }
    public boolean itemDropsSupported() { return diagnostics().connected() && itemDropsSupported; }
    public boolean videoGpuSupported() { return diagnostics().connected() && videoGpuSupported; }
    public boolean terrainMovementReady() { return diagnostics().connected() && terrainMovementReady; }
    public String renderStatus() {
        String mask=maskCountsAvailable ? "マスク="+maskForeground+"/"+maskPixels+" 半透明="+maskTranslucent : "マスク=GPU内（画素数読戻しなし）";
        return "照明="+lightingEnabled+" MC空="+vanillaSkyEnabled+" "+mask+" / "+videoColor+" / 持ち物="+heldModel
                +" モデル="+itemModelCount+" 飛行="+flying+" 地形待ち="+!terrainMovementReady;
    }
    public String blockModelError() {return blockModelError;}
    public String mobReason() { return mobReason; }
    public String mobStatus() { return "モブ="+mobCount+" 素材不足="+mobMissing+" 卵用素材="+mobTemplateCount+" 理由="+mobReason; }
    private ParticleDiagnostics particles = ParticleDiagnostics.unavailable("unsupported");
    public ParticleDiagnostics particleDiagnostics() {
        return diagnostics().connected() ? particles : ParticleDiagnostics.unavailable("disconnected");
    }
    public boolean playerVisualsSupported() { return diagnostics().connected() && playerVisualsSupported; }
    public JsonObject pollFeedback() { return feedback.pollFirst(); }
    /** Item results are acknowledged only after the game applies the inventory transaction. */
    public JsonObject pollItemFeedback() { return itemFeedback.pollFirst(); }
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
    private record Pending(byte[] bytes, long sequence, long created, long sent, boolean attempted) {}
    public record EventReceipt(boolean accepted, String reason) {}
    public EventReceipt eventReceipt(String id) { return eventReceipts.get(id); }
    public boolean eventPending(String id) { return pending.containsKey(id); }
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
    /** Reliable application protocols send their own retries; these are not movement samples. */
    public void auxiliary(JsonObject p) throws IOException { send(encode(p)); }
    public void event(JsonObject p) throws IOException {
        if (pending.size() >= MAX_PENDING_EVENTS) throw new IOException("Bridge event queue is full");
        String id = UUID.randomUUID().toString(); p.addProperty("eventId", id);
        byte[] bytes = encode(p);
        pending.put(id, new Pending(bytes, p.get("seq").getAsLong(), clock.getAsLong(), 0, false));
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
    private static String diagnosticText(JsonObject p,String key) {
        var value=p.get(key);return value!=null && value.isJsonPrimitive() && value.getAsJsonPrimitive().isString() && value.getAsString().length()<=256 ? value.getAsString() : "";
    }
    private static int diagnosticCount(JsonObject p,String key,int limit) {
        double value=number(p,key) ? p.get(key).getAsDouble() : 0;return Double.isFinite(value) && value==Math.rint(value) && value>=0 && value<=limit ? (int)value : 0;
    }
    private static boolean number(JsonObject p, String name) {
        return p.has(name) && p.get(name).isJsonPrimitive() && p.getAsJsonPrimitive(name).isNumber();
    }
    private static boolean bool(JsonObject p, String name) {
        return p.has(name) && p.get(name).isJsonPrimitive() && p.getAsJsonPrimitive(name).isBoolean();
    }
    private static boolean string(JsonObject p, String name, int maximum) {
        return p.has(name) && p.get(name).isJsonPrimitive() && p.getAsJsonPrimitive(name).isString()
                && p.get(name).getAsString().length() <= maximum;
    }
    private static boolean integer(JsonObject p, String name, long minimum, long maximum) {
        if (!number(p,name)) return false;
        double value=p.get(name).getAsDouble();
        return Double.isFinite(value) && value==Math.rint(value) && value>=minimum && value<=maximum;
    }
    private static boolean uuid(JsonObject p, String name) {
        if (!string(p,name,36) || p.get(name).getAsString().length()!=36) return false;
        String value=p.get(name).getAsString();
        try { return UUID.fromString(value).toString().equals(value.toLowerCase(java.util.Locale.ROOT)); }
        catch (IllegalArgumentException invalid) { return false; }
    }
    private boolean currentReceiverReply(JsonObject p) {
        if (!diagnostics().connected() || !string(p,"receiverId",36)
                || receiverId.isEmpty() || !receiverId.equals(p.get("receiverId").getAsString())
                || !integer(p,"seq",1,sequence)) return false;
        Long sent=inputTimes.get(p.get("seq").getAsLong());
        return sent!=null && clock.getAsLong()-sent<=1_000_000_000L;
    }
    private void acknowledgeEvent(JsonObject p) {
        if (!uuid(p,"eventId")) return;
        String id=p.get("eventId").getAsString(); Pending event=pending.get(id);
        if (event==null) return;
        boolean enhanced=p.has("accepted") || p.has("reason");
        if ((p.has("accepted") && !bool(p,"accepted")) || (p.has("reason") && !string(p,"reason",160))) return;
        if (enhanced && (!diagnostics().connected() || !p.has("receiverId") || !p.has("seq"))) return;
        if (p.has("receiverId") && (!string(p,"receiverId",36) || receiverId.isEmpty()
                || !receiverId.equals(p.get("receiverId").getAsString()))) return;
        if (p.has("seq") && (!integer(p,"seq",1,sequence) || p.get("seq").getAsLong()!=event.sequence)) return;
        pending.remove(id); ++acknowledged;
        eventReceipts.put(id,new EventReceipt(!p.has("accepted") || p.get("accepted").getAsBoolean(),
                p.has("reason") ? p.get("reason").getAsString() : ""));
        if (eventReceipts.size()>MAX_EVENT_RECEIPTS) eventReceipts.remove(eventReceipts.keySet().iterator().next());
    }
    private void receiveItemResult(JsonObject p) {
        if (!itemDropsSupported() || !currentReceiverReply(p) || !uuid(p,"itemTx")
                || !integer(p,"itemRevision",0,100000) || !integer(p,"itemCount",0,99)
                || !string(p,"itemAction",16) || !string(p,"itemReason",160)) return;
        String action=p.get("itemAction").getAsString();
        if (!"spawned".equals(action) && !"rejected".equals(action)
                && !"pickup".equals(action) && !"expired".equals(action)) return;
        // Do not deduplicate or ACK here: application retries resolve inventory changes exactly once.
        if (itemFeedback.size()<MAX_ITEM_FEEDBACK) itemFeedback.addLast(p.deepCopy());
    }
    private static double performanceNumber(JsonObject p,String name,double maximum) {
        if (!number(p,name)) return -1;
        double value=p.get(name).getAsDouble();
        return Double.isFinite(value) && value>=0 && value<=maximum ? value : -1;
    }
    private static int performanceCount(JsonObject p,String name,int maximum) {
        return integer(p,name,0,maximum) ? p.get(name).getAsInt() : -1;
    }
    private void receivePerformance(JsonObject p) {
        if (!currentReceiverReply(p)) return;
        performance=new PerformanceDiagnostics(true,performanceNumber(p,"fps",10000),
                performanceNumber(p,"frameMs",60000),performanceNumber(p,"renderMs",60000),
                performanceNumber(p,"renderGpuMs",60000),performanceCount(p,"faces",20_000_000),
                performanceCount(p,"renderSections",1_000_000),performanceCount(p,"cells",20000),
                performanceCount(p,"nearCollisionCells",20000),performanceNumber(p,"buildMs",60000),
                performanceCount(p,"streamPending",20000),performanceCount(p,"lightPending",20_000_000),
                performanceCount(p,"itemCount",4096),performanceNumber(p,"videoReadyMs",60000),
                integer(p,"videoDropped",0,9_007_199_254_740_991L) ? p.get("videoDropped").getAsLong() : -1,
                string(p,"videoTransport",48) ? p.get("videoTransport").getAsString() : "",
                string(p,"gpuReason",160) ? p.get("gpuReason").getAsString() : "");
        performanceTime=clock.getAsLong();
    }
    private void reply(JsonObject p) throws IOException {
        if (!number(p, "v") || p.get("v").getAsDouble() != 1
                || !string(p,"session",36) || !session.equals(p.get("session").getAsString())
                || !string(p,"kind",32)) return;
        String kind = p.get("kind").getAsString();
        if ("ack".equals(kind)) {
            acknowledgeEvent(p);
        } else if ("item_result".equals(kind)) {
            receiveItemResult(p);
        } else if ("perf_status".equals(kind)) {
            receivePerformance(p);
        } else if ("feedback".equals(kind) || "mob_feedback".equals(kind)) {
            if(!feedbackSupported || !diagnostics().connected()) return;
            if("mob_feedback".equals(kind) ? !MobFeedbackData.valid(p) : VanillaFeedbackData.parse(p)==null) return;
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
            terrainSupported=bool(p,"terrainV2") && p.get("terrainV2").getAsBoolean();
            itemDropsSupported=bool(p,"itemDropsV1") && p.get("itemDropsV1").getAsBoolean();
            videoGpuSupported=bool(p,"videoGpuV1") && p.get("videoGpuV1").getAsBoolean();
            blockPaletteReady=bool(p,"blockPaletteReady") && p.get("blockPaletteReady").getAsBoolean();
            mobPaletteReady=bool(p,"mobPaletteReady") && p.get("mobPaletteReady").getAsBoolean();
            mobsSupported=bool(p,"mobsV1") && p.get("mobsV1").getAsBoolean();
            skySupported=bool(p,"skySupported") && p.get("skySupported").getAsBoolean();
            lightingEnabled=!bool(p,"lightingEnabled") || p.get("lightingEnabled").getAsBoolean();
            vanillaSkyEnabled=bool(p,"vanillaSkyEnabled") && p.get("vanillaSkyEnabled").getAsBoolean();
            maskCountsAvailable=!bool(p,"maskCountsAvailable") || p.get("maskCountsAvailable").getAsBoolean();
            terrainMovementReady=!bool(p,"terrainMovementReady") || p.get("terrainMovementReady").getAsBoolean();
            double mobs=number(p,"mobCount") ? p.get("mobCount").getAsDouble() : 0;
            double missing=number(p,"mobMissing") ? p.get("mobMissing").getAsDouble() : 0;
            mobCount=Double.isFinite(mobs) && mobs==Math.rint(mobs) && mobs>=0 && mobs<=128 ? (int)mobs : 0;
            mobMissing=Double.isFinite(missing) && missing==Math.rint(missing) && missing>=0 && missing<=100000 ? (int)missing : 0;
            double hp=number(p,"uePlayerHealth") ? p.get("uePlayerHealth").getAsDouble() : 20;
            playerHealth=Double.isFinite(hp) && hp>=0 && hp<=20 ? hp : 20;
            maskPixels=statusCount(p,"maskPixels",1920*1080);maskForeground=statusCount(p,"maskForeground",maskPixels);
            maskTranslucent=statusCount(p,"maskTranslucent",maskForeground);
            heldModel=diagnosticText(p,"heldModel");videoColor=diagnosticText(p,"videoColor");
            mobReason=string(p,"mobReason",160) ? p.get("mobReason").getAsString() : "";
            flying=bool(p,"flying") && p.get("flying").getAsBoolean();
            itemModelCount=diagnosticCount(p,"itemModelCount",4096);mobTemplateCount=diagnosticCount(p,"mobTemplateCount",128);
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
            importedCells=Double.isFinite(cells) && cells==Math.rint(cells) && cells>=0 && cells<=20000 ? (int)cells : 0;
            worldSupported=bool(p,"worldV1") && p.get("worldV1").getAsBoolean();
            videoSupported=bool(p,"videoV1") && p.get("videoV1").getAsBoolean();
            texturesSupported=bool(p,"blockTexturesV1") && p.get("blockTexturesV1").getAsBoolean();
            videoControlsSupported=bool(p,"videoControlsV1") && p.get("videoControlsV1").getAsBoolean();
            double materials=number(p,"textureMaterials") ? p.get("textureMaterials").getAsDouble() : 0;
            textureMaterials=Double.isFinite(materials) && materials==Math.rint(materials) && materials>=0 && materials<=4096 ? (int)materials : 0;
            String nextReceiver=p.has("receiverId") && p.get("receiverId").isJsonPrimitive() && p.getAsJsonPrimitive("receiverId").isString()
                    ? p.get("receiverId").getAsString() : "";
            if(!receiverId.equals(nextReceiver)) { pose=null; poseSequence=0; feedback.clear();seenFeedback.clear();itemFeedback.clear();eventReceipts.clear();performance=noPerformance(); }
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
                send(p.bytes); e.setValue(new Pending(p.bytes, p.sequence, p.created, now, true));
            }
        }
    }
    private static int statusCount(JsonObject p,String name,int maximum) {
        double value=number(p,name) ? p.get(name).getAsDouble() : 0;
        return Double.isFinite(value) && value==Math.rint(value) && value>=0 && value<=maximum ? (int)value : 0;
    }
    @Override public void close() throws IOException { pending.clear(); eventReceipts.clear(); inputTimes.clear(); feedback.clear();seenFeedback.clear();itemFeedback.clear();channel.close(); }
}

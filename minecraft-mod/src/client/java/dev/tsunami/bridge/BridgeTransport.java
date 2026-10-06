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
    private long sequence, lastStatus, lastStatusSequence, sentInputs, acknowledged, expired, lastRtt;
    private boolean hasStatus, cameraReady, vfxReady;
    private int walls;
    private String receiver = "unknown";
    private String build = "unknown";
    private String receiverId = "";
    private boolean worldSupported, videoSupported;
    private record Pending(byte[] bytes, long created, long sent, boolean attempted) {}
    public record Diagnostics(boolean connected, boolean cameraReady, boolean vfxReady, int walls,
                              int pending, long sentInputs, long acknowledged, long expired, double rttMillis, String receiver,
                              String build, boolean worldSupported, boolean videoSupported, String receiverId) {}

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
                build, connected && worldSupported, connected && videoSupported, receiverId);
    }
    private static boolean number(JsonObject p, String name) {
        return p.has(name) && p.get(name).isJsonPrimitive() && p.getAsJsonPrimitive(name).isNumber();
    }
    private static boolean bool(JsonObject p, String name) {
        return p.has(name) && p.get(name).isJsonPrimitive() && p.getAsJsonPrimitive(name).isBoolean();
    }
    private void reply(JsonObject p) {
        if (!number(p, "v") || p.get("v").getAsDouble() != 1
                || !session.equals(p.get("session").getAsString())) return;
        String kind = p.get("kind").getAsString();
        if ("ack".equals(kind)) {
            if (pending.remove(p.get("eventId").getAsString()) != null) ++acknowledged;
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
            worldSupported=bool(p,"worldV1") && p.get("worldV1").getAsBoolean();
            videoSupported=bool(p,"videoV1") && p.get("videoV1").getAsBoolean();
            receiverId=p.has("receiverId") && p.get("receiverId").isJsonPrimitive() && p.getAsJsonPrimitive("receiverId").isString()
                    ? p.get("receiverId").getAsString() : "";
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
    @Override public void close() throws IOException { pending.clear(); inputTimes.clear(); channel.close(); }
}

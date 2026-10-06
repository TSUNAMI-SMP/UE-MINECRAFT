package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.DatagramChannel;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.UUID;

/** Independent of Fabric/Minecraft. Nonblocking, bounded loopback datagrams. */
public final class BridgeTransport implements AutoCloseable {
    private final DatagramChannel channel;
    private final String session = UUID.randomUUID().toString();
    private final ByteBuffer receive = ByteBuffer.allocate(4096);
    private final LinkedHashMap<String, Pending> pending = new LinkedHashMap<>();
    private long sequence;
    private record Pending(byte[] bytes, long created, long sent) {}

    public BridgeTransport(int port) throws IOException {
        channel = DatagramChannel.open();
        channel.bind(new InetSocketAddress("127.0.0.1", 0));
        channel.connect(new InetSocketAddress("127.0.0.1", port));
        channel.configureBlocking(false);
    }
    public JsonObject packet(String kind) {
        JsonObject p = new JsonObject();
        p.addProperty("v", 1); p.addProperty("kind", kind); p.addProperty("session", session);
        p.addProperty("seq", ++sequence); return p;
    }
    private void send(byte[] bytes) throws IOException {
        if (bytes.length > 2048) throw new IOException("Bridge packet exceeds 2048 bytes");
        channel.write(ByteBuffer.wrap(bytes));
    }
    public void input(JsonObject p) throws IOException { send(encode(p)); }
    public void event(JsonObject p) throws IOException {
        if (pending.size() >= 64) throw new IOException("Bridge event queue is full");
        String id = UUID.randomUUID().toString(); p.addProperty("eventId", id);
        byte[] bytes = encode(p); long now = System.nanoTime();
        pending.put(id, new Pending(bytes, now, 0));
        pump();
    }
    public void pump() throws IOException {
        for (int i = 0; i < 32; i++) {
            receive.clear(); if (channel.read(receive) <= 0) break;
            receive.flip();
            try {
                JsonObject ack = JsonParser.parseString(StandardCharsets.UTF_8.decode(receive).toString()).getAsJsonObject();
                if (ack.get("v").getAsInt() == 1 && "ack".equals(ack.get("kind").getAsString())
                        && session.equals(ack.get("session").getAsString())) pending.remove(ack.get("eventId").getAsString());
            } catch (RuntimeException ignored) { /* Malformed datagram; ignore. */ }
        }
        long now = System.nanoTime();
        var it = pending.entrySet().iterator();
        while (it.hasNext()) {
            var e = it.next(); Pending p = e.getValue();
            if (now - p.created > 2_000_000_000L) { it.remove(); continue; }
            if (now - p.sent >= 100_000_000L) {
                send(p.bytes); e.setValue(new Pending(p.bytes, p.created, now));
            }
        }
    }
    private static byte[] encode(JsonObject p) { return p.toString().getBytes(StandardCharsets.UTF_8); }
    @Override public void close() throws IOException { pending.clear(); channel.close(); }
}

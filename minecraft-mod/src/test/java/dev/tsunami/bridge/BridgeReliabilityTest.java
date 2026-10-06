package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.junit.Test;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicLong;
import static org.junit.Assert.*;

public class BridgeReliabilityTest {
    private DatagramPacket receive(DatagramSocket socket) throws Exception {
        DatagramPacket p = new DatagramPacket(new byte[4096], 4096); socket.receive(p); return p;
    }
    private JsonObject json(DatagramPacket p) {
        return JsonParser.parseString(new String(p.getData(), 0, p.getLength(), StandardCharsets.UTF_8)).getAsJsonObject();
    }
    private void reply(DatagramSocket ue, DatagramPacket input, JsonObject p) throws Exception {
        byte[] data = p.toString().getBytes(StandardCharsets.UTF_8);
        ue.send(new DatagramPacket(data, data.length, input.getSocketAddress()));
    }
    private void pumpReplies(BridgeTransport mc) throws Exception {
        for (int i = 0; i < 10; i++) { mc.pump(); Thread.sleep(2); }
    }
    @Test public void expiryAndRetryUseMonotonicTimeStartingAtZero() throws Exception {
        AtomicLong time = new AtomicLong();
        try (DatagramSocket ue = new DatagramSocket(0, InetAddress.getLoopbackAddress());
             BridgeTransport mc = new BridgeTransport(ue.getLocalPort(), time::get)) {
            ue.setSoTimeout(150); mc.event(mc.packet("event")); JsonObject initial = json(receive(ue));
            time.set(100_000_000L); mc.pump(); assertEquals(initial, json(receive(ue)));
            time.set(2_000_000_000L); mc.pump();
            assertEquals(0, mc.diagnostics().pending()); assertEquals(1, mc.diagnostics().expired());
            try { receive(ue); fail("Expired event sent again"); } catch (java.net.SocketTimeoutException expected) { }
        }
    }
    @Test public void oversizeEventNeverPollutesQueue() throws Exception {
        try (DatagramSocket ue = new DatagramSocket(0, InetAddress.getLoopbackAddress());
             BridgeTransport mc = new BridgeTransport(ue.getLocalPort())) {
            JsonObject p = mc.packet("event"); p.addProperty("payload", "爆".repeat(800));
            try { mc.event(p); fail("oversize UTF-8 accepted"); } catch (java.io.IOException expected) { }
            assertEquals(0, mc.diagnostics().pending()); assertEquals(64, mc.availableEvents());
            mc.event(mc.packet("event")); assertEquals(1, mc.diagnostics().pending());
        }
    }
    @Test public void statusRequiresValidSessionSequenceAndExactVersion() throws Exception {
        AtomicLong time = new AtomicLong(1_000_000_000L);
        try (DatagramSocket ue = new DatagramSocket(0, InetAddress.getLoopbackAddress());
             BridgeTransport mc = new BridgeTransport(ue.getLocalPort(), time::get)) {
            ue.setSoTimeout(500); mc.input(mc.packet("input")); DatagramPacket input = receive(ue); JsonObject data = json(input);
            JsonObject status = new JsonObject(); status.addProperty("v", 1); status.addProperty("kind", "status");
            status.add("session", data.get("session")); status.add("seq", data.get("seq"));
            status.addProperty("cameraReady", true); status.addProperty("vfxReady", false); status.addProperty("walls", 2);
            status.addProperty("v", 1.5); reply(ue, input, status); pumpReplies(mc); assertFalse(mc.diagnostics().connected());
            status.addProperty("v", 1); status.addProperty("seq", 999); reply(ue, input, status); pumpReplies(mc); assertFalse(mc.diagnostics().connected());
            status.add("seq", data.get("seq")); status.addProperty("walls", -1); reply(ue, input, status); pumpReplies(mc); assertFalse(mc.diagnostics().connected());
            status.addProperty("walls", 2); time.addAndGet(30_000_000L); reply(ue, input, status); pumpReplies(mc);
            assertTrue(mc.diagnostics().connected()); assertTrue(mc.diagnostics().cameraReady()); assertFalse(mc.diagnostics().vfxReady());
            assertEquals(2, mc.diagnostics().walls()); assertEquals(30.0, mc.diagnostics().rttMillis(), 0.001);
            time.addAndGet(1_000_000_001L); assertFalse(mc.diagnostics().connected());
            reply(ue, input, status); pumpReplies(mc); assertFalse("Replay must not renew readiness", mc.diagnostics().connected());
        }
    }
    @Test public void fractionalVersionAckCannotRemoveEvent() throws Exception {
        try (DatagramSocket ue = new DatagramSocket(0, InetAddress.getLoopbackAddress());
             BridgeTransport mc = new BridgeTransport(ue.getLocalPort())) {
            ue.setSoTimeout(500); mc.event(mc.packet("event")); DatagramPacket input = receive(ue); JsonObject data = json(input);
            JsonObject ack = new JsonObject(); ack.addProperty("v", 1.5); ack.addProperty("kind", "ack");
            ack.add("session", data.get("session")); ack.add("eventId", data.get("eventId"));
            reply(ue, input, ack); pumpReplies(mc); assertEquals(1, mc.diagnostics().pending());
            ack.addProperty("v", 1); reply(ue, input, ack); pumpReplies(mc);
            assertEquals(0, mc.diagnostics().pending()); assertEquals(1, mc.diagnostics().acknowledged());
            reply(ue, input, ack); pumpReplies(mc); assertEquals("Duplicate ACK counted twice", 1, mc.diagnostics().acknowledged());
        }
    }
}

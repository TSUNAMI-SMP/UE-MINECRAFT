package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.junit.Test;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
import static org.junit.Assert.*;

public class BridgeTransportTest {
    private DatagramPacket receive(DatagramSocket socket) throws Exception {
        DatagramPacket p = new DatagramPacket(new byte[4096], 4096); socket.receive(p); return p;
    }
    private JsonObject json(DatagramPacket p) {
        return JsonParser.parseString(new String(p.getData(), 0, p.getLength(), StandardCharsets.UTF_8)).getAsJsonObject();
    }
    @Test public void inputAndReliableEventRoundTrip() throws Exception {
        try (DatagramSocket ue = new DatagramSocket(0, InetAddress.getLoopbackAddress());
             BridgeTransport mc = new BridgeTransport(ue.getLocalPort())) {
            ue.setSoTimeout(500);
            JsonObject input = mc.packet("input"); input.addProperty("yaw", 45); mc.input(input);
            JsonObject received = json(receive(ue)); assertEquals(45, received.get("yaw").getAsInt());
            assertEquals(1, received.get("v").getAsInt());
            JsonObject event = mc.packet("event"); event.addProperty("event", "tnt_ignite"); mc.event(event);
            DatagramPacket first = receive(ue); JsonObject firstJson = json(first);
            assertTrue(firstJson.get("seq").getAsLong() > received.get("seq").getAsLong());
            Thread.sleep(120); mc.pump();
            JsonObject retry = json(receive(ue)); assertEquals(firstJson, retry);
            JsonObject ack = new JsonObject(); ack.addProperty("v", 1); ack.addProperty("kind", "ack");
            ack.add("session", firstJson.get("session")); ack.add("eventId", firstJson.get("eventId"));
            byte[] bytes = ack.toString().getBytes(StandardCharsets.UTF_8);
            ue.send(new DatagramPacket(bytes, bytes.length, first.getSocketAddress()));
            // Poll for ACK delivery, then cross a retransmission interval.
            for (int i = 0; i < 10; i++) { Thread.sleep(10); mc.pump(); }
            Thread.sleep(120); mc.pump(); ue.setSoTimeout(150);
            try { receive(ue); fail("ACKed event retransmitted"); }
            catch (java.net.SocketTimeoutException expected) { }
        }
    }
    @Test public void wrongSessionAckCannotCancelEvent() throws Exception {
        try (DatagramSocket ue = new DatagramSocket(0, InetAddress.getLoopbackAddress());
             BridgeTransport mc = new BridgeTransport(ue.getLocalPort())) {
            ue.setSoTimeout(500); JsonObject event = mc.packet("event"); mc.event(event);
            DatagramPacket first = receive(ue); JsonObject p = json(first);
            String ack = "{\"v\":1,\"kind\":\"ack\",\"session\":\"wrong\",\"eventId\":\""+p.get("eventId").getAsString()+"\"}";
            byte[] bytes = ack.getBytes(StandardCharsets.UTF_8);
            ue.send(new DatagramPacket(bytes, bytes.length, first.getSocketAddress()));
            Thread.sleep(120); mc.pump(); assertEquals(p, json(receive(ue)));
        }
    }
    @Test public void oversizedPacketRejected() throws Exception {
        try (DatagramSocket ue = new DatagramSocket(0, InetAddress.getLoopbackAddress());
             BridgeTransport mc = new BridgeTransport(ue.getLocalPort())) {
            JsonObject p = mc.packet("input"); p.addProperty("payload", "x".repeat(2048));
            try { mc.input(p); fail("oversized packet accepted"); }
            catch (java.io.IOException expected) { }
        }
    }
}

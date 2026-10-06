package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicLong;
import org.junit.Test;
import static org.junit.Assert.*;

/** Real loopback transport regression: validate before ACK, bound and deduplicate effects. */
public class BridgeFeedbackTransportTest {
    private static final String RECEIVER="00000000-0000-4000-8000-000000000099";
    private static DatagramPacket receive(DatagramSocket socket) throws Exception {
        DatagramPacket packet=new DatagramPacket(new byte[4096],4096);socket.receive(packet);return packet;
    }
    private static JsonObject json(DatagramPacket packet) {
        return JsonParser.parseString(new String(packet.getData(),0,packet.getLength(),StandardCharsets.UTF_8)).getAsJsonObject();
    }
    private static void send(DatagramSocket ue,DatagramPacket destination,JsonObject packet) throws Exception {
        byte[] bytes=packet.toString().getBytes(StandardCharsets.UTF_8);
        ue.send(new DatagramPacket(bytes,bytes.length,destination.getSocketAddress()));
    }
    private static void pump(BridgeTransport mc) throws Exception {
        for(int i=0;i<5;i++) {mc.pump();Thread.sleep(5);}
    }
    private static DatagramPacket connect(DatagramSocket ue,BridgeTransport mc) throws Exception {
        ue.setSoTimeout(150);
        mc.input(mc.packet("input"));DatagramPacket destination=receive(ue);
        JsonObject status=new JsonObject();status.addProperty("v",1);status.addProperty("kind","status");
        status.addProperty("session",mc.session());status.add("seq",json(destination).get("seq"));
        status.addProperty("cameraReady",true);status.addProperty("vfxReady",false);status.addProperty("walls",0);
        status.addProperty("receiverId",RECEIVER);status.addProperty("vanillaFeedbackV1",true);status.addProperty("playerVisualsV1",true);
        send(ue,destination,status);pump(mc);assertTrue(mc.playerVisualsSupported());return destination;
    }
    private static JsonObject effect(BridgeTransport mc) {
        JsonObject p=new JsonObject();p.addProperty("v",1);p.addProperty("kind","feedback");
        p.addProperty("session",mc.session());p.addProperty("receiverId",RECEIVER);p.addProperty("effectId",UUID.randomUUID().toString());
        p.addProperty("seq",1);p.addProperty("type","break");p.addProperty("block","minecraft:stone");
        for(String key:new String[]{"x","y","z","listenerX","listenerY","listenerZ","listenerYaw","listenerPitch","fallDistance"}) p.addProperty(key,0);
        return p;
    }
    @Test public void duplicateFeedbackIsAckedAgainButDeliveredOnce() throws Exception {
        try(DatagramSocket ue=new DatagramSocket(0,InetAddress.getLoopbackAddress());BridgeTransport mc=new BridgeTransport(ue.getLocalPort())) {
            DatagramPacket destination=connect(ue,mc);
            for(String type:new String[]{"break","place","step","land"}) {
                JsonObject effect=effect(mc); effect.addProperty("type",type); effect.addProperty("fallDistance",1.25);
                send(ue,destination,effect);pump(mc);
                JsonObject ack=json(receive(ue));assertEquals("feedback_ack",ack.get("kind").getAsString());
                assertEquals(effect.get("effectId"),ack.get("effectId"));assertEquals(effect,mc.pollFeedback());
                send(ue,destination,effect);pump(mc);assertEquals(effect.get("effectId"),json(receive(ue)).get("effectId"));
                assertNull(mc.pollFeedback());
            }
        }
    }
    @Test public void invalidStaleAndForeignEffectsCannotBeAckedOrPlayed() throws Exception {
        AtomicLong clock=new AtomicLong(1_000_000_000L);
        try(DatagramSocket ue=new DatagramSocket(0,InetAddress.getLoopbackAddress());BridgeTransport mc=new BridgeTransport(ue.getLocalPort(),clock::get)) {
            DatagramPacket destination=connect(ue,mc);
            JsonObject malformed=effect(mc);malformed.addProperty("listenerPitch",91);send(ue,destination,malformed);
            JsonObject foreign=effect(mc);foreign.addProperty("receiverId",UUID.randomUUID().toString());send(ue,destination,foreign);
            JsonObject unknownInput=effect(mc);unknownInput.addProperty("seq",500);send(ue,destination,unknownInput);
            JsonObject wrongSession=effect(mc);wrongSession.addProperty("session",UUID.randomUUID().toString());send(ue,destination,wrongSession);
            pump(mc);assertNull(mc.pollFeedback());
            clock.addAndGet(1_000_000_001L);send(ue,destination,effect(mc));pump(mc);assertNull(mc.pollFeedback());
            try {receive(ue);fail("Invalid effect ACKed");} catch(java.net.SocketTimeoutException expected) { }
        }
    }
    @Test public void uppercaseUnrealIdsAndMobSoundsAreDeliveredAndDeduplicated() throws Exception {
        try(DatagramSocket ue=new DatagramSocket(0,InetAddress.getLoopbackAddress());BridgeTransport mc=new BridgeTransport(ue.getLocalPort())) {
            DatagramPacket destination=connect(ue,mc);
            for(boolean mob:new boolean[]{false,true}) {
                JsonObject p=effect(mc);p.addProperty("effectId","ABCDEF01-2345-4678-9ABC-DEF012345678");
                if(mob) {
                    p.addProperty("effectId","ABCDEF02-2345-4678-9ABC-DEF012345678");
                    p.addProperty("kind","mob_feedback");p.addProperty("type","mob");p.addProperty("sound","minecraft:entity.zombie.hurt");
                    p.addProperty("lx",2);p.addProperty("ly",0);p.addProperty("lz",3);
                }
                send(ue,destination,p);pump(mc);assertEquals(p.get("effectId"),json(receive(ue)).get("effectId"));
                assertEquals(p,mc.pollFeedback());
                send(ue,destination,p);pump(mc);assertEquals(p.get("effectId"),json(receive(ue)).get("effectId"));assertNull(mc.pollFeedback());
            }
        }
    }
}

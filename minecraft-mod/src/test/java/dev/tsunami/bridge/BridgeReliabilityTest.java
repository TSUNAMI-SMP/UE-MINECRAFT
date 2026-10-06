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
            assertFalse("Old receiver must not enable world traffic",mc.diagnostics().worldSupported());
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
    @Test public void receiverBuildCapabilitiesAndRestartIdAreVisible() throws Exception {
        try (DatagramSocket ue=new DatagramSocket(0,InetAddress.getLoopbackAddress()); BridgeTransport mc=new BridgeTransport(ue.getLocalPort())) {
            ue.setSoTimeout(500);
            for (String id:new String[]{"00000000-0000-4000-8000-000000000010","00000000-0000-4000-8000-000000000011"}) {
                mc.input(mc.packet("input")); var input=receive(ue); var data=json(input);
                JsonObject status=new JsonObject(); status.addProperty("v",1); status.addProperty("kind","status");
                status.add("session",data.get("session")); status.add("seq",data.get("seq"));
                status.addProperty("cameraReady",true); status.addProperty("vfxReady",false); status.addProperty("walls",0);
                status.addProperty("build","0.4.0"); status.addProperty("receiverId",id); status.addProperty("worldV1",true); status.addProperty("videoV1",true);
                status.addProperty("blockTexturesV1",true); status.addProperty("videoControlsV1",true); status.addProperty("textureMaterials",42);
                reply(ue,input,status); pumpReplies(mc);
                assertEquals("0.4.0",mc.diagnostics().build()); assertEquals(id,mc.diagnostics().receiverId());
                assertTrue(mc.diagnostics().worldSupported()); assertTrue(mc.diagnostics().videoSupported());
                assertTrue(mc.diagnostics().texturesSupported()); assertTrue(mc.diagnostics().videoControlsSupported()); assertEquals(42,mc.diagnostics().textureMaterials());
            }
        }
    }

    @Test public void authorityPoseRejectsWrongReceiverReplayAndExpires() throws Exception {
        AtomicLong time=new AtomicLong(1_000_000_000L);
        try(DatagramSocket ue=new DatagramSocket(0,InetAddress.getLoopbackAddress()); BridgeTransport mc=new BridgeTransport(ue.getLocalPort(),time::get)) {
            ue.setSoTimeout(500); mc.input(mc.packet("input")); var input=receive(ue); var data=json(input);
            var status=new JsonObject(); status.addProperty("v",1);status.addProperty("kind","status");
            status.add("session",data.get("session"));status.add("seq",data.get("seq"));
            status.addProperty("cameraReady",true);status.addProperty("vfxReady",false);status.addProperty("walls",0);
            status.addProperty("authorityV1",true);status.addProperty("worldSealed",true);status.addProperty("ueControl",true);
            status.addProperty("receiverId","receiver");status.addProperty("importId","import");status.addProperty("importedCells",75);
            reply(ue,input,status);pumpReplies(mc);
            assertTrue(mc.diagnostics().authoritySupported());assertTrue(mc.diagnostics().worldSealed());assertTrue(mc.diagnostics().ueControl());
            assertEquals(75,mc.diagnostics().importedCells());
            var pose=status.deepCopy();pose.addProperty("kind","pose");pose.addProperty("poseSeq",1);
            pose.addProperty("x",1);pose.addProperty("y",2);pose.addProperty("z",3);pose.addProperty("grounded",true);
            pose.addProperty("receiverId","wrong");reply(ue,input,pose);pumpReplies(mc);assertNull(mc.authorityPose());
            pose.addProperty("receiverId","receiver");pose.addProperty("grounded","true");reply(ue,input,pose);pumpReplies(mc);assertNull(mc.authorityPose());
            pose.addProperty("grounded",true);reply(ue,input,pose);pumpReplies(mc);assertEquals(1,mc.authorityPose().x(),0);
            pose.addProperty("x",9);reply(ue,input,pose);pumpReplies(mc);assertEquals(1,mc.authorityPose().x(),0);
            pose.addProperty("poseSeq",2);pose.addProperty("seq",999);reply(ue,input,pose);pumpReplies(mc);assertEquals(1,mc.authorityPose().x(),0);
            time.addAndGet(1_000_000_001L);assertNull(mc.authorityPose());
        }
    }
    @Test public void capturedInputAgeAndActionCapabilitiesRequireRealStatus() throws Exception {
        AtomicLong time=new AtomicLong(1_000_000_000L);
        try(DatagramSocket ue=new DatagramSocket(0,InetAddress.getLoopbackAddress());BridgeTransport mc=new BridgeTransport(ue.getLocalPort(),time::get)) {
            ue.setSoTimeout(500);var p=mc.packet("input");mc.input(p);var input=receive(ue);var data=json(input);
            assertFalse(mc.actionsSupported());assertEquals(-1,mc.inputAgeMillis(99),0);
            time.addAndGet(40_000_000L);assertEquals(40,mc.inputAgeMillis(data.get("seq").getAsLong()),0);
            var status=new JsonObject();status.addProperty("v",1);status.addProperty("kind","status");
            status.add("session",data.get("session"));status.add("seq",data.get("seq"));
            status.addProperty("cameraReady",true);status.addProperty("vfxReady",false);status.addProperty("walls",0);
            status.addProperty("blockActionsV1",true);status.addProperty("videoV2",true);status.addProperty("lastAction","placed");
            reply(ue,input,status);pumpReplies(mc);assertTrue(mc.actionsSupported());assertTrue(mc.videoV2Supported());assertEquals("placed",mc.lastAction());
            time.addAndGet(1_000_000_001L);assertFalse(mc.actionsSupported());assertFalse(mc.videoV2Supported());
        }
    }
    @Test public void particleDiagnosticsAreOptionalValidatedAndExpireWithStatus() throws Exception {
        AtomicLong time=new AtomicLong(1_000_000_000L);
        try(DatagramSocket ue=new DatagramSocket(0,InetAddress.getLoopbackAddress());BridgeTransport mc=new BridgeTransport(ue.getLocalPort(),time::get)) {
            ue.setSoTimeout(500);
            for(int mode=0;mode<4;mode++) {
                mc.input(mc.packet("input")); var input=receive(ue); var data=json(input);
                JsonObject status=mode==0 ? new JsonObject() : ParticleDiagnosticsTest.status();
                status.addProperty("v",1); status.addProperty("kind","status");
                status.add("session",data.get("session")); status.add("seq",data.get("seq"));
                status.addProperty("cameraReady",true); status.addProperty("vfxReady",false); status.addProperty("walls",0);
                if(mode==2) status.getAsJsonObject("particles").addProperty("lastReason","bad\nreason");
                reply(ue,input,status); pumpReplies(mc);
                assertTrue("Optional diagnostic error must not lose UE readiness",mc.diagnostics().connected());
                assertEquals(mode==1 || mode==3,mc.particleDiagnostics().supported());
                if(mode==2) assertEquals("invalid-diagnostics",mc.particleDiagnostics().reason());
            }
            time.addAndGet(1_000_000_001L);
            assertFalse(mc.particleDiagnostics().ready()); assertFalse(mc.particleDiagnostics().supported());
            assertEquals("disconnected",mc.particleDiagnostics().reason());
        }
    }
}

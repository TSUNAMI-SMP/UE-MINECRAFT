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

/** Loopback tests for application receipts, inventory transaction boundaries and optional metrics. */
public class BridgeTransportEnhancementsTest {
    private static final String RECEIVER="00000000-0000-4000-8000-000000000099";
    private static DatagramPacket receive(DatagramSocket socket) throws Exception {
        DatagramPacket packet=new DatagramPacket(new byte[4096],4096); socket.receive(packet); return packet;
    }
    private static JsonObject json(DatagramPacket packet) {
        return JsonParser.parseString(new String(packet.getData(),0,packet.getLength(),StandardCharsets.UTF_8)).getAsJsonObject();
    }
    private static void send(DatagramSocket socket,DatagramPacket destination,JsonObject packet) throws Exception {
        byte[] bytes=packet.toString().getBytes(StandardCharsets.UTF_8);
        socket.send(new DatagramPacket(bytes,bytes.length,destination.getSocketAddress()));
    }
    private static JsonObject reply(BridgeTransport mc,String kind,long seq) {
        JsonObject p=new JsonObject();p.addProperty("v",1);p.addProperty("kind",kind);p.addProperty("session",mc.session());
        p.addProperty("receiverId",RECEIVER);p.addProperty("seq",seq);return p;
    }
    private static JsonObject status(BridgeTransport mc,long seq) {
        JsonObject p=reply(mc,"status",seq);p.addProperty("cameraReady",true);p.addProperty("vfxReady",false);p.addProperty("walls",0);
        p.addProperty("itemDropsV1",true);p.addProperty("terrainV2",true);p.addProperty("videoGpuV1",false);
        p.addProperty("maskCountsAvailable",false);p.addProperty("terrainMovementReady",false);
        p.addProperty("importedCells",8125);p.addProperty("mobReason","missing_template:minecraft:zombie");return p;
    }
    private static DatagramPacket connect(DatagramSocket socket,BridgeTransport mc) throws Exception {
        socket.setSoTimeout(150);mc.input(mc.packet("input"));DatagramPacket destination=receive(socket);
        send(socket,destination,status(mc,json(destination).get("seq").getAsLong()));mc.pump();
        assertTrue(mc.diagnostics().connected());return destination;
    }
    private static JsonObject ack(BridgeTransport mc,JsonObject event) {
        JsonObject p=reply(mc,"ack",event.get("seq").getAsLong());p.add("eventId",event.get("eventId"));
        p.addProperty("accepted",true);p.addProperty("reason","");return p;
    }
    private static JsonObject item(BridgeTransport mc) {
        JsonObject p=reply(mc,"item_result",1);p.addProperty("itemTx",UUID.randomUUID().toString());
        p.addProperty("itemRevision",1);p.addProperty("itemAction","pickup");p.addProperty("itemCount",3);p.addProperty("itemReason","");return p;
    }
    @Test public void enhancedAckReportsRejectAndCannotCancelAnotherOrReplayedEvent() throws Exception {
        AtomicLong clock=new AtomicLong(1_000_000_000L);
        try(DatagramSocket ue=new DatagramSocket(0,InetAddress.getLoopbackAddress());BridgeTransport mc=new BridgeTransport(ue.getLocalPort(),clock::get)) {
            DatagramPacket destination=connect(ue,mc);JsonObject event=mc.packet("mob_spawn");mc.event(event);receive(ue);
            String id=event.get("eventId").getAsString();assertTrue(mc.eventPending(id));
            JsonObject foreign=ack(mc,event);foreign.addProperty("receiverId",UUID.randomUUID().toString());send(ue,destination,foreign);
            JsonObject future=ack(mc,event);future.addProperty("seq",event.get("seq").getAsLong()+1);send(ue,destination,future);
            JsonObject wrongEventSequence=ack(mc,event);wrongEventSequence.addProperty("seq",1);send(ue,destination,wrongEventSequence);
            JsonObject malformed=ack(mc,event);malformed.addProperty("accepted","false");send(ue,destination,malformed);
            JsonObject longReason=ack(mc,event);longReason.addProperty("reason","x".repeat(161));send(ue,destination,longReason);
            mc.pump();assertTrue(mc.eventPending(id));assertNull(mc.eventReceipt(id));
            JsonObject rejected=ack(mc,event);rejected.addProperty("accepted",false);rejected.addProperty("reason","spawn_blocked");
            send(ue,destination,rejected);mc.pump();assertFalse(mc.eventPending(id));
            assertEquals(new BridgeTransport.EventReceipt(false,"spawn_blocked"),mc.eventReceipt(id));
            send(ue,destination,ack(mc,event));mc.pump();assertFalse(mc.eventReceipt(id).accepted());
            assertEquals(1,mc.diagnostics().acknowledged());
        }
    }
    @Test public void legacyAckDefaultsToAcceptedAndReceiptsAreBounded() throws Exception {
        AtomicLong clock=new AtomicLong(1_000_000_000L);
        try(DatagramSocket ue=new DatagramSocket(0,InetAddress.getLoopbackAddress());BridgeTransport mc=new BridgeTransport(ue.getLocalPort(),clock::get)) {
            DatagramPacket destination=connect(ue,mc);String firstId="",lastId="";
            for(int n=0;n<=BridgeTransport.MAX_EVENT_RECEIPTS;n++) {
                JsonObject event=mc.packet("mob_spawn");mc.event(event);receive(ue);
                JsonObject p=new JsonObject();p.addProperty("v",1);p.addProperty("kind","ack");p.addProperty("session",mc.session());p.add("eventId",event.get("eventId"));
                send(ue,destination,p);mc.pump();lastId=event.get("eventId").getAsString();if(n==0)firstId=lastId;
                assertEquals(new BridgeTransport.EventReceipt(true,""),mc.eventReceipt(lastId));
            }
            assertNull(mc.eventReceipt(firstId));assertNotNull(mc.eventReceipt(lastId));assertEquals(0,mc.diagnostics().pending());
        }
    }
    @Test public void itemResultsRequireApplicationResolutionAndAuxiliaryIsNotInput() throws Exception {
        try(DatagramSocket ue=new DatagramSocket(0,InetAddress.getLoopbackAddress());BridgeTransport mc=new BridgeTransport(ue.getLocalPort())) {
            DatagramPacket destination=connect(ue,mc);JsonObject p=item(mc);
            send(ue,destination,p);send(ue,destination,p);mc.pump();
            assertEquals(p,mc.pollItemFeedback());assertEquals(p,mc.pollItemFeedback());assertNull(mc.pollFeedback());
            try { receive(ue);fail("Inventory result ACKed before application"); } catch(java.net.SocketTimeoutException expected) { }
            JsonObject resolve=mc.packet("item_resolve");resolve.add("itemTx",p.get("itemTx"));mc.auxiliary(resolve);
            assertEquals(resolve,json(receive(ue)));assertEquals(1,mc.diagnostics().sentInputs());
            assertEquals(-1,mc.inputAgeMillis(resolve.get("seq").getAsLong()),0);
        }
    }
    @Test public void malformedForeignAndStaleItemResultsAreNotDelivered() throws Exception {
        AtomicLong clock=new AtomicLong(1_000_000_000L);
        try(DatagramSocket ue=new DatagramSocket(0,InetAddress.getLoopbackAddress());BridgeTransport mc=new BridgeTransport(ue.getLocalPort(),clock::get)) {
            DatagramPacket destination=connect(ue,mc);
            for(String key:new String[]{"itemTx","itemRevision","itemAction","itemCount","itemReason","receiverId","seq","session"}) {
                JsonObject p=item(mc);
                switch(key) {
                    case "itemTx" -> p.addProperty(key,"not-a-uuid");
                    case "itemRevision" -> p.addProperty(key,1.5);
                    case "itemAction" -> p.addProperty(key,"delete");
                    case "itemCount" -> p.addProperty(key,100);
                    case "itemReason" -> p.addProperty(key,"x".repeat(161));
                    case "seq" -> p.addProperty(key,2);
                    default -> p.addProperty(key,UUID.randomUUID().toString());
                }
                send(ue,destination,p);
            }
            mc.pump();assertNull(mc.pollItemFeedback());
            clock.addAndGet(1_000_000_001L);send(ue,destination,item(mc));mc.pump();assertNull(mc.pollItemFeedback());
            try { receive(ue);fail("Invalid item result ACKed"); } catch(java.net.SocketTimeoutException expected) { }
        }
    }
    @Test public void itemQueueIsBoundedAndReceiverChangeClearsResultsAndReceipts() throws Exception {
        AtomicLong clock=new AtomicLong(1_000_000_000L);
        try(DatagramSocket ue=new DatagramSocket(0,InetAddress.getLoopbackAddress());BridgeTransport mc=new BridgeTransport(ue.getLocalPort(),clock::get)) {
            DatagramPacket destination=connect(ue,mc);
            for(int n=0;n<90;n++) send(ue,destination,item(mc));
            for(int n=0;n<3;n++) mc.pump();
            int received=0;while(mc.pollItemFeedback()!=null) ++received;assertEquals(64,received);
            JsonObject event=mc.packet("mob_spawn");mc.event(event);receive(ue);send(ue,destination,ack(mc,event));mc.pump();
            assertNotNull(mc.eventReceipt(event.get("eventId").getAsString()));send(ue,destination,item(mc));mc.pump();
            mc.input(mc.packet("input"));DatagramPacket nextInput=receive(ue);
            JsonObject next=status(mc,json(nextInput).get("seq").getAsLong());next.addProperty("receiverId",UUID.randomUUID().toString());
            send(ue,destination,next);mc.pump();assertNull(mc.pollItemFeedback());assertNull(mc.eventReceipt(event.get("eventId").getAsString()));
        }
    }
    @Test public void capabilitiesLargerImportsAndOptionalMeasurementsRemainExplicit() throws Exception {
        try(DatagramSocket ue=new DatagramSocket(0,InetAddress.getLoopbackAddress());BridgeTransport mc=new BridgeTransport(ue.getLocalPort())) {
            DatagramPacket destination=connect(ue,mc);assertTrue(mc.terrainSupported());assertTrue(mc.itemDropsSupported());assertFalse(mc.videoGpuSupported());
            assertFalse(mc.terrainMovementReady());assertTrue(mc.renderStatus().contains("マスク=GPU内（画素数読戻しなし）"));assertTrue(mc.renderStatus().contains("地形待ち=true"));
            assertEquals(8125,mc.diagnostics().importedCells());assertTrue(mc.mobStatus().contains("missing_template:minecraft:zombie"));
            JsonObject p=reply(mc,"perf_status",1);p.addProperty("fps",30);p.addProperty("frameMs",33.3);p.addProperty("faces",65536);p.addProperty("cells",8125);
            p.addProperty("videoReadyMs",2.5);p.addProperty("videoDropped",7);p.addProperty("videoTransport","JPEG/TCP");p.addProperty("gpuReason","DX12 fallback");
            p.addProperty("renderGpuMs","33.3");p.addProperty("renderSections",-1);p.addProperty("buildMs",1e100);send(ue,destination,p);mc.pump();
            BridgeTransport.PerformanceDiagnostics metrics=mc.performanceDiagnostics();assertTrue(metrics.available());
            assertEquals(30,metrics.fps(),0);assertEquals(65536,metrics.faces());assertEquals(8125,metrics.cells());
            assertEquals(-1,metrics.renderGpuMs(),0);assertEquals(-1,metrics.renderSections());assertEquals(-1,metrics.buildMs(),0);
            assertEquals(33.3,metrics.frameMs(),0);assertEquals(2.5,metrics.videoReadyMs(),0);assertEquals(7,metrics.videoDropped());
            assertTrue(mc.perfStatus().contains("DX12 fallback"));assertTrue(mc.perfStatus().contains("転送=JPEG/TCP"));
            assertFalse(mc.perfStatus().contains(" GPU="));assertTrue(mc.perfStatus().contains("render=?"));
            JsonObject foreign=p.deepCopy();foreign.addProperty("receiverId",UUID.randomUUID().toString());foreign.addProperty("fps",99);send(ue,destination,foreign);mc.pump();
            assertEquals(30,mc.performanceDiagnostics().fps(),0);
        }
    }
}

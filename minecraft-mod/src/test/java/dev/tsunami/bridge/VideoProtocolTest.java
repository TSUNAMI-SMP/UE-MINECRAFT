package dev.tsunami.bridge;

import java.io.*;
import java.net.*;
import java.awt.image.BufferedImage;
import java.nio.charset.StandardCharsets;
import javax.imageio.ImageIO;
import org.junit.Test;
import static org.junit.Assert.*;

public class VideoProtocolTest {
    private byte[] frame(int advertisedWidth,int magic) throws Exception {
        var image=new BufferedImage(32,16,BufferedImage.TYPE_INT_RGB);
        for(int y=0;y<16;y++) for(int x=0;x<32;x++) image.setRGB(x,y,x<16 ? 0xff0000 : 0x0000ff);
        var jpeg=new ByteArrayOutputStream(); assertTrue(ImageIO.write(image,"jpeg",jpeg));
        var result=new ByteArrayOutputStream(); var out=new DataOutputStream(result);
        out.writeInt(magic); out.writeInt(1); out.writeInt(advertisedWidth); out.writeInt(16);
        out.writeInt(-1); out.writeInt(jpeg.size()); out.write(jpeg.toByteArray()); return result.toByteArray();
    }
    @Test public void decodeJpegDimensionsSequenceAndOrientation() throws Exception {
        var f=VideoProtocol.read(new DataInputStream(new ByteArrayInputStream(frame(32,VideoProtocol.MAGIC))));
        assertEquals(32,f.width()); assertEquals(16,f.height()); assertEquals(4294967295L,f.sequence());
        assertTrue((f.argb()[0]&0xff0000)>0xe00000); assertTrue((f.argb()[31]&0xff)>220);
    }
    @Test public void rejectWrongDimensionsMagicAndTruncatedFrame() throws Exception {
        byte[] valid=frame(32,VideoProtocol.MAGIC);
        for(byte[] data:new byte[][]{frame(33,VideoProtocol.MAGIC),frame(32,0),java.util.Arrays.copyOf(valid,valid.length-1)}) {
            try { VideoProtocol.read(new DataInputStream(new ByteArrayInputStream(data))); fail(); } catch(IOException expected) { }
        }
    }
    @Test public void rejectHugePayloadBeforeReadingOrAllocatingIt() throws Exception {
        var bytes=new ByteArrayOutputStream(); var out=new DataOutputStream(bytes);
        for(int v:new int[]{VideoProtocol.MAGIC,1,1920,1080,1,Integer.MAX_VALUE}) out.writeInt(v);
        try { VideoProtocol.read(new DataInputStream(new ByteArrayInputStream(bytes.toByteArray()))); fail(); } catch(IOException expected) { }
    }
    @Test public void actualLoopbackHandshakeAndFragmentedStream() throws Exception {
        String session="01234567-1234-1234-1234-0123456789ab"; byte[] bytes=frame(32,VideoProtocol.MAGIC);
        try(var server=new ServerSocket(0,1,InetAddress.getByName("127.0.0.1")); var client=new VideoClient(server.getLocalPort(),session)) {
            server.setSoTimeout(2000);
            try(var peer=server.accept()) {
                peer.setSoTimeout(2000);
                assertEquals("UEBH"+session,new String(peer.getInputStream().readNBytes(40),StandardCharsets.US_ASCII));
                var output=peer.getOutputStream();
                for(int i=0;i<bytes.length;i+=13) output.write(bytes,i,Math.min(13,bytes.length-i)); output.flush();
                long deadline=System.nanoTime()+2_000_000_000L; VideoProtocol.Frame got=null;
                while(got==null && System.nanoTime()<deadline) { got=client.poll(); if(got==null) Thread.sleep(5); }
                assertNotNull("No decoded frame arrived from loopback",got); assertTrue(client.fresh());
                assertNull("Only the latest frame is retained",client.poll());
            }
        }
    }
}

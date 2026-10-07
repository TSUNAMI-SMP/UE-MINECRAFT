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
    private byte[] versionTwo(long input,long gpu,long encode) throws Exception {
        byte[] v1=frame(32,VideoProtocol.MAGIC);
        var bytes=new ByteArrayOutputStream();var out=new DataOutputStream(bytes);
        var in=new DataInputStream(new ByteArrayInputStream(v1));
        out.writeInt(in.readInt());in.readInt();out.writeInt(2);
        for(int i=0;i<4;i++) out.writeInt(in.readInt());
        out.writeLong(input);out.writeInt((int)gpu);out.writeInt((int)encode);out.write(in.readAllBytes());return bytes.toByteArray();
    }
    @Test public void versionTwoEchoesInputAndTimingsWithoutChangingPixels() throws Exception {
        var f=VideoProtocol.read(new DataInputStream(new ByteArrayInputStream(versionTwo(9007199254740991L,12500,2500))));
        assertEquals(9007199254740991L,f.inputSequence());assertEquals(12.5,f.readbackMs(),0);assertEquals(2.5,f.encodeMs(),0);
        assertTrue(f.decodeMs()>=0);assertTrue(f.decodedAt()>0);
        assertTrue((f.abgr()[0]&255)>220);assertTrue((f.abgr()[31]&0xff0000)>0xe00000);
    }
    @Test public void invalidTimingMetadataRejected() throws Exception {
        for(byte[] bytes:new byte[][]{versionTwo(-1,1,1),versionTwo(9007199254740992L,1,1),versionTwo(1,10000001,1),versionTwo(1,1,0xffffffffL)}) {
            try {VideoProtocol.read(new DataInputStream(new ByteArrayInputStream(bytes)));fail();}catch(IOException expected) {}
        }
    }
    @Test public void bulkRgbaConversionPreservesAlphaAndChannelOrder() {
        assertArrayEquals(new int[]{0x44332211,0xff0000ff,0xffff0000},VideoProtocol.toAbgr(new int[]{0x44112233,0xffff0000,0xff0000ff}));
    }
    private byte[] versionThree(int flags,byte[] mask,double cameraX) throws Exception {
        var in=new DataInputStream(new ByteArrayInputStream(versionTwo(7,1000,2000)));
        var bytes=new ByteArrayOutputStream();var out=new DataOutputStream(bytes);
        out.writeInt(in.readInt());in.readInt();out.writeInt(3);
        for(int i=0;i<4;i++) out.writeInt(in.readInt());
        out.writeLong(in.readLong());out.writeInt(in.readInt());out.writeInt(in.readInt());
        out.writeInt(flags);out.writeInt(mask.length);
        out.writeDouble(cameraX);out.writeDouble(65.62);out.writeDouble(-2);
        out.writeFloat(180);out.writeFloat(-30);out.writeFloat(92);
        out.write(in.readAllBytes());out.write(mask);return bytes.toByteArray();
    }
    @Test public void versionThreeMatchesCameraAndLosslessOpacityToOneFrame() throws Exception {
        byte[] mask={0,16,0,1,(byte)240,(byte)255}; // 16 background +496 foreground pixels.
        var f=VideoProtocol.read(new DataInputStream(new ByteArrayInputStream(versionThree(1,mask,12.5))));
        assertTrue(f.skyMask());assertEquals(7,f.inputSequence());assertEquals(4294967295L,f.sequence());
        assertEquals(12.5,f.camera().x(),0);assertEquals(65.62,f.camera().y(),0);assertEquals(92,f.camera().verticalFov(),0);
        assertEquals(0,f.argb()[0]>>>24);assertEquals(255,f.argb()[16]>>>24);assertEquals(0,f.abgr()[15]>>>24);
    }
    @Test public void versionThreeNormalFrameKeepsOpaqueJpegAndCamera() throws Exception {
        var f=VideoProtocol.read(new DataInputStream(new ByteArrayInputStream(versionThree(0,new byte[0],0))));
        assertFalse(f.skyMask());assertEquals(255,f.argb()[0]>>>24);assertNotNull(f.camera());
    }
    @Test public void versionThreeRejectsUnknownFlagsCameraAndBadMaskBeforeDisplay() throws Exception {
        for(byte[] bytes:new byte[][]{
            versionThree(2,new byte[0],0),versionThree(1,new byte[0],0),versionThree(0,new byte[]{2,0,(byte)255},0),
            versionThree(1,new byte[]{0,0,0},0),versionThree(1,new byte[]{2,1,0},0),versionThree(1,new byte[]{1,0,0},0),
            versionThree(1,new byte[]{0,1},0),versionThree(0,new byte[0],Double.NaN),versionThree(0,new byte[0],30_000_001)}) {
            try {VideoProtocol.read(new DataInputStream(new ByteArrayInputStream(bytes)));fail();}catch(IOException expected) {}
        }
    }
    @Test public void versionThreeRejectsTruncatedMaskInsteadOfUsingPreviousFrameMask() throws Exception {
        byte[] bytes=versionThree(1,new byte[]{2,0,(byte)255},0);
        try {VideoProtocol.read(new DataInputStream(new ByteArrayInputStream(java.util.Arrays.copyOf(bytes,bytes.length-1))));fail();}
        catch(EOFException expected) {}
    }
    @Test public void translucentCaptureIsConvertedToStraightAlphaWithoutDarkeningTwice() throws Exception {
        // Half-opaque red captures linear0.5 as gamma2.2 red186. Its source color must recover to255 before GUI blending.
        int[] pixels={0xffba0000,0xff123456,0xffaabbcc};
        VideoProtocol.applyMask(pixels,new byte[]{0,1,(byte)128,0,1,0,0,1,(byte)255});
        assertEquals(128,pixels[0]>>>24);assertTrue(((pixels[0]>>>16)&255)>=254);
        assertEquals(0,pixels[1]);assertEquals(0xffaabbcc,pixels[2]);
        int sky=200,red=(pixels[0]>>>16)&255,alpha=pixels[0]>>>24;
        assertEquals(228,(int)Math.round(red*alpha/255.0+sky*(1-alpha/255.0)),1);
    }
    @Test public void v3HandshakeAndFrameLatchKeepSkyAndImageTogether() throws Exception {
        String session="01234567-1234-1234-1234-0123456789ab";
        byte[] bytes=versionThree(1,new byte[]{2,0,(byte)255},100);
        try(var server=new ServerSocket(0,1,InetAddress.getByName("127.0.0.1"));var client=new VideoClient(server.getLocalPort(),session,true)) {
            server.setSoTimeout(2000);
            try(var peer=server.accept()) {
                peer.setSoTimeout(2000);
                assertEquals("UEB3"+session,new String(peer.getInputStream().readNBytes(40),StandardCharsets.US_ASCII));
                for(int i=0;i<bytes.length;i+=11) peer.getOutputStream().write(bytes,i,Math.min(11,bytes.length-i));
                peer.getOutputStream().flush();
                long deadline=System.nanoTime()+2_000_000_000L;
                while(!VanillaSkyComposite.active() && System.nanoTime()<deadline) {VanillaSkyComposite.prepare(client,true);Thread.sleep(5);}
                assertTrue(VanillaSkyComposite.active());var displayed=VanillaSkyComposite.displayed(client);
                assertEquals(100,VanillaSkyComposite.camera().x(),0);
                VanillaSkyComposite.prepare(client,true);assertSame(displayed,VanillaSkyComposite.displayed(client));
                assertEquals(92,VanillaSkyComposite.projectionFov(1920,960),.001); // Fixture32x16 uses2:1, not16:9.
                assertEquals(98.7151,VanillaSkyComposite.projectionFov(1920,1080),.001);
                assertTrue(VanillaSkyComposite.projectionFov(1024,1024)>92);
                VanillaSkyComposite.prepare(client,false);assertFalse(VanillaSkyComposite.active());assertNull(VanillaSkyComposite.camera());
            }
        } finally {VanillaSkyComposite.prepare(null,false);}
    }
    @Test public void versionFourStraightAlphaDoesNotBrightenDecodedJpegAgain() throws Exception {
        var image=new BufferedImage(32,16,BufferedImage.TYPE_INT_RGB);
        for(int y=0;y<16;y++) for(int x=0;x<32;x++) image.setRGB(x,y,0x406080);
        var jpeg=new ByteArrayOutputStream();assertTrue(ImageIO.write(image,"jpeg",jpeg));
        int expected=ImageIO.read(new ByteArrayInputStream(jpeg.toByteArray())).getRGB(0,0)&0xffffff;
        var bytes=new ByteArrayOutputStream();var out=new DataOutputStream(bytes);
        out.writeInt(VideoProtocol.MAGIC);out.writeInt(4);out.writeInt(32);out.writeInt(16);out.writeInt(1);out.writeInt(jpeg.size());
        out.writeLong(7);out.writeInt(0);out.writeInt(0);out.writeInt(1);out.writeInt(3);
        out.writeDouble(0);out.writeDouble(65.62);out.writeDouble(0);out.writeFloat(0);out.writeFloat(0);out.writeFloat(80);
        out.write(jpeg.toByteArray());out.write(new byte[]{2,0,(byte)128});
        var frame=VideoProtocol.read(new DataInputStream(new ByteArrayInputStream(bytes.toByteArray())));
        assertTrue(frame.skyMask());assertEquals(128,frame.argb()[0]>>>24);assertEquals(expected,frame.argb()[0]&0xffffff);
        assertEquals(expected,frame.argb()[511]&0xffffff);
    }
    @Test public void straightAlphaPreservesColorWhileLegacyStillUnpremultiplies() throws Exception {
        int[] nativeColor={0xff406080},legacy={0xff406080};byte[] mask={0,1,(byte)128};
        VideoProtocol.applyMask(nativeColor,mask,false);VideoProtocol.applyMask(legacy,mask,true);
        assertEquals(0x80406080,nativeColor[0]);assertTrue((legacy[0]&255)>(nativeColor[0]&255));
    }
    private byte[] sharedFrame(int flags,int payload,int mask,long handle,int slot,int generation) throws Exception {
        var bytes=new ByteArrayOutputStream();var out=new DataOutputStream(bytes);
        for(int v:new int[]{VideoProtocol.MAGIC,5,1920,1080,42,payload})out.writeInt(v);
        out.writeLong(123);out.writeInt(1250);out.writeInt(0);out.writeInt(flags);out.writeInt(mask);
        out.writeDouble(2);out.writeDouble(65.62);out.writeDouble(-4);out.writeFloat(45);out.writeFloat(-12);out.writeFloat(58.72f);
        out.writeLong(handle);out.writeLong(0x12345678abcdef01L);out.writeInt(slot);out.writeInt(generation);
        return bytes.toByteArray();
    }
    @Test public void gpuFrameCarriesMatchedCameraAlphaAndLeaseWithoutAllocatingPixels() throws Exception {
        var frame=VideoProtocol.read(new DataInputStream(new ByteArrayInputStream(sharedFrame(3,0,0,0x1234,2,9))));
        assertEquals(1920,frame.width());assertEquals(1080,frame.height());assertEquals(0,frame.abgr().length);
        assertTrue(frame.skyMask());assertEquals(123,frame.inputSequence());assertEquals(42,frame.sequence());
        assertEquals(2,frame.camera().x(),0);assertEquals(0x1234,frame.gpu().handle());assertEquals(2,frame.gpu().slot());
        assertEquals(9,frame.gpu().generation());assertEquals(1.25,frame.readbackMs(),0);
    }
    @Test public void rejectInvalidGpuMetadataBeforeOpeningAnyNativeResource() throws Exception {
        for(byte[] bytes:new byte[][]{sharedFrame(1,0,0,1,0,1),sharedFrame(6,0,0,1,0,1),sharedFrame(2,4,0,1,0,1),
            sharedFrame(3,0,3,1,0,1),sharedFrame(2,0,0,0,0,1),sharedFrame(2,0,0,1,3,1),sharedFrame(2,0,0,1,0,0)}) {
            try {VideoProtocol.read(new DataInputStream(new ByteArrayInputStream(bytes)));fail();}catch(IOException expected){}
        }
    }
    @Test public void gpuHandshakeLeaseReleaseAndJpegFallbackTravelThroughRealLoopback() throws Exception {
        String session="01234567-1234-1234-1234-0123456789ab";
        try(var server=new ServerSocket(0,1,InetAddress.getByName("127.0.0.1"));var client=new VideoClient(server.getLocalPort(),session,true,true)) {
            server.setSoTimeout(2000);
            try(var peer=server.accept()) {
                peer.setSoTimeout(2000);var input=new DataInputStream(peer.getInputStream());
                assertEquals("UEB5"+session,new String(input.readNBytes(40),StandardCharsets.US_ASCII));
                peer.getOutputStream().write(sharedFrame(3,0,0,0x1234,2,9));peer.getOutputStream().flush();
                long deadline=System.nanoTime()+2_000_000_000L;VideoProtocol.Frame frame=null;
                while(frame==null && System.nanoTime()<deadline){frame=client.poll();if(frame==null)Thread.sleep(5);}
                assertNotNull(frame);client.release(frame);
                assertEquals(0x55454241,input.readInt());assertEquals(42,input.readInt());assertEquals(2,input.readInt());assertEquals(9,input.readInt());
                client.disableGpu("test fallback");assertEquals(0x55454246,input.readInt());
                assertEquals(0,input.readInt());assertEquals(0,input.readInt());assertEquals(0,input.readInt());
            }
        }
    }

}

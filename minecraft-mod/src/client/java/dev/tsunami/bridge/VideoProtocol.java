package dev.tsunami.bridge;

import java.io.*;
import java.awt.image.BufferedImage;
import javax.imageio.ImageIO;
import javax.imageio.ImageReader;
import javax.imageio.stream.MemoryCacheImageInputStream;

/** Bounded JPEG stream. v3 adds a matched, lossless opacity mask and captured camera. */
public final class VideoProtocol {
    public static final int MAGIC = 0x55454256, MAX_BYTES = 2*1024*1024;
    public static final int SKY_MASK=1, MAX_MASK_BYTES=1920*1080*3;
    private static final int[][] STRAIGHT_COLOR=straightColorTable();
    public record Camera(double x,double y,double z,float yaw,float pitch,float verticalFov) {}
    public record Frame(int width,int height,long sequence,int[] argb,int[] abgr,long inputSequence,
                        double readbackMs,double encodeMs,double decodeMs,long decodedAt,boolean skyMask,Camera camera) {}
    static int[] toAbgr(int[] argb) {
        int[] result=new int[argb.length];
        for(int i=0;i<argb.length;i++) {int v=argb[i];result[i]=(v&0xff00ff00)|((v&255)<<16)|((v>>>16)&255);}
        return result;
    }
    public static Frame read(DataInputStream in) throws IOException {
        if(in.readInt()!=MAGIC) throw new IOException("Unsupported UE video protocol");
        int version=in.readInt();if(version<1 || version>4) throw new IOException("Unsupported UE video protocol");
        int width = in.readInt(), height = in.readInt(); long sequence = Integer.toUnsignedLong(in.readInt());
        int length = in.readInt();
        if (width < 16 || height < 16 || width > 1920 || height > 1080 || length < 4 || length > MAX_BYTES)
            throw new IOException("Invalid UE video dimensions/length");
        long inputSequence=0;double readbackMs=0,encodeMs=0;
        if(version>=2) {
            inputSequence=in.readLong();readbackMs=Integer.toUnsignedLong(in.readInt())/1000.0;encodeMs=Integer.toUnsignedLong(in.readInt())/1000.0;
            if(inputSequence<0 || inputSequence>9_007_199_254_740_991L || readbackMs>10000 || encodeMs>10000) throw new IOException("Invalid video timing metadata");
        }
        int maskLength=0;boolean skyMask=false;Camera camera=null;
        if(version>=3) {
            int flags=in.readInt();maskLength=in.readInt();
            camera=new Camera(in.readDouble(),in.readDouble(),in.readDouble(),in.readFloat(),in.readFloat(),in.readFloat());
            skyMask=(flags&SKY_MASK)!=0;
            if((flags&~SKY_MASK)!=0 || maskLength<0 || maskLength>MAX_MASK_BYTES || maskLength%3!=0
                || skyMask!=(maskLength>0) || maskLength>(long)width*height*3 || !validCamera(camera))
                throw new IOException("Invalid video mask/camera metadata");
        }
        byte[] bytes = new byte[length]; in.readFully(bytes);
        byte[] mask=new byte[maskLength];in.readFully(mask);long decodeStart=System.nanoTime();
        try (var imageInput = new MemoryCacheImageInputStream(new ByteArrayInputStream(bytes))) {
            var readers = ImageIO.getImageReaders(imageInput);
            if (!readers.hasNext()) throw new IOException("Invalid JPEG");
            ImageReader reader = readers.next();
            try {
                if (!"JPEG".equalsIgnoreCase(reader.getFormatName())) throw new IOException("Expected JPEG");
                reader.setInput(imageInput, true, true);
                // Reject oversized decoded images before allocating their pixels.
                if (reader.getWidth(0) != width || reader.getHeight(0) != height) throw new IOException("JPEG dimensions mismatch");
                BufferedImage image = reader.read(0);
                int[] argb=image.getRGB(0,0,width,height,null,0,width);
                if(skyMask) applyMask(argb,mask,version<4);
                int[] abgr=toAbgr(argb);
                long decodedAt=System.nanoTime();
                return new Frame(width,height,sequence,argb,abgr,inputSequence,readbackMs,encodeMs,(decodedAt-decodeStart)/1_000_000.0,decodedAt,skyMask,camera);
            } finally { reader.dispose(); }
        }
    }
    private static boolean validCamera(Camera c) {
        return Double.isFinite(c.x()) && Math.abs(c.x())<=30_000_000 && Double.isFinite(c.y()) && Math.abs(c.y())<=30_000_000
            && Double.isFinite(c.z()) && Math.abs(c.z())<=30_000_000 && Float.isFinite(c.yaw()) && Math.abs(c.yaw())<=1e9
            && Float.isFinite(c.pitch()) && Math.abs(c.pitch())<=90 && Float.isFinite(c.verticalFov()) && c.verticalFov()>=10 && c.verticalFov()<=150;
    }
    static void applyMask(int[] pixels,byte[] runs) throws IOException {applyMask(pixels,runs,true);}
    static void applyMask(int[] pixels,byte[] runs,boolean legacyPremultiplied) throws IOException {
        int offset=0;
        for(int i=0;i<runs.length;i+=3) {
            int count=((runs[i]&255)<<8)|(runs[i+1]&255),alpha=runs[i+2]&255;
            if(count==0 || count>pixels.length-offset) throw new IOException("Invalid opacity run");
            for(int j=0;j<count;j++,offset++) {
                int color=pixels[offset];
                // UE's black-background capture contains opacity-weighted linear light, encoded with TargetGamma2.2.
                // GUI_TEXTURED uses straight alpha: undo that weight before Minecraft multiplies it once.
                if(alpha==0) color=0;
                else if(alpha<255 && legacyPremultiplied) color=(STRAIGHT_COLOR[alpha][(color>>>16)&255]<<16)
                    |(STRAIGHT_COLOR[alpha][(color>>>8)&255]<<8)|STRAIGHT_COLOR[alpha][color&255];
                pixels[offset]=(color&0xffffff)|(alpha<<24);
            }
        }
        if(offset!=pixels.length) throw new IOException("Incomplete opacity mask");
    }
    private static int[][] straightColorTable() {
        int[][] table=new int[256][256];
        for(int alpha=1;alpha<256;alpha++) for(int value=0;value<256;value++)
            table[alpha][value]=(int)Math.round(Math.pow(Math.min(1,Math.pow(value/255.0,2.2)*255.0/alpha),1/2.2)*255);
        return table;
    }
}

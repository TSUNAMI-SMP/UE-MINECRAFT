package dev.tsunami.bridge;

import java.io.*;
import java.awt.image.BufferedImage;
import javax.imageio.ImageIO;
import javax.imageio.ImageReader;
import javax.imageio.stream.MemoryCacheImageInputStream;

/** Bounded JPEG stream. v2 echoes the captured input and reports GPU/encode timings. */
public final class VideoProtocol {
    public static final int MAGIC = 0x55454256, MAX_BYTES = 2*1024*1024;
    public record Frame(int width,int height,long sequence,int[] argb,int[] abgr,long inputSequence,
                        double readbackMs,double encodeMs,double decodeMs,long decodedAt) {}
    static int[] toAbgr(int[] argb) {
        int[] result=new int[argb.length];
        for(int i=0;i<argb.length;i++) {int v=argb[i];result[i]=(v&0xff00ff00)|((v&255)<<16)|((v>>>16)&255);}
        return result;
    }
    public static Frame read(DataInputStream in) throws IOException {
        if(in.readInt()!=MAGIC) throw new IOException("Unsupported UE video protocol");
        int version=in.readInt();if(version!=1 && version!=2) throw new IOException("Unsupported UE video protocol");
        int width = in.readInt(), height = in.readInt(); long sequence = Integer.toUnsignedLong(in.readInt());
        int length = in.readInt();
        if (width < 16 || height < 16 || width > 1920 || height > 1080 || length < 4 || length > MAX_BYTES)
            throw new IOException("Invalid UE video dimensions/length");
        long inputSequence=0;double readbackMs=0,encodeMs=0;
        if(version==2) {
            inputSequence=in.readLong();readbackMs=Integer.toUnsignedLong(in.readInt())/1000.0;encodeMs=Integer.toUnsignedLong(in.readInt())/1000.0;
            if(inputSequence<0 || inputSequence>9_007_199_254_740_991L || readbackMs>10000 || encodeMs>10000) throw new IOException("Invalid video timing metadata");
        }
        byte[] bytes = new byte[length]; in.readFully(bytes);long decodeStart=System.nanoTime();
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
                int[] argb=image.getRGB(0,0,width,height,null,0,width),abgr=toAbgr(argb);
                long decodedAt=System.nanoTime();
                return new Frame(width,height,sequence,argb,abgr,inputSequence,readbackMs,encodeMs,(decodedAt-decodeStart)/1_000_000.0,decodedAt);
            } finally { reader.dispose(); }
        }
    }
}

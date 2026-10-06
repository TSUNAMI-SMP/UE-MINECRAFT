package dev.tsunami.bridge;

import java.io.*;
import java.awt.image.BufferedImage;
import javax.imageio.ImageIO;
import javax.imageio.ImageReader;
import javax.imageio.stream.MemoryCacheImageInputStream;

/** UEBV v1: six network-order uint32 words, followed by a bounded JPEG. */
public final class VideoProtocol {
    public static final int MAGIC = 0x55454256, MAX_BYTES = 2*1024*1024;
    public record Frame(int width, int height, long sequence, int[] argb) {}
    public static Frame read(DataInputStream in) throws IOException {
        if (in.readInt() != MAGIC || in.readInt() != 1) throw new IOException("Unsupported UE video protocol");
        int width = in.readInt(), height = in.readInt(); long sequence = Integer.toUnsignedLong(in.readInt());
        int length = in.readInt();
        if (width < 16 || height < 16 || width > 1920 || height > 1080 || length < 4 || length > MAX_BYTES)
            throw new IOException("Invalid UE video dimensions/length");
        byte[] bytes = new byte[length]; in.readFully(bytes);
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
                return new Frame(width,height,sequence,image.getRGB(0,0,width,height,null,0,width));
            } finally { reader.dispose(); }
        }
    }
}

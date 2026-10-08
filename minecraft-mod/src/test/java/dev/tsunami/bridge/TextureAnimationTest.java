package dev.tsunami.bridge;
import com.google.gson.JsonParser;
import java.awt.image.BufferedImage;
import java.io.IOException;
import org.junit.Test;
import static org.junit.Assert.*;

public class TextureAnimationTest {
    private BufferedImage frames() {
        var image=new BufferedImage(2,4,BufferedImage.TYPE_INT_ARGB);
        for(int y=0;y<4;y++) for(int x=0;x<2;x++) image.setRGB(x,y,y<2 ? 0xffff0000 : 0xff0000ff);
        return image;
    }
    private TextureAnimation.Strip bake(String json) throws IOException {return TextureAnimation.bake(frames(),JsonParser.parseString(json).getAsJsonObject());}
    @Test public void uniformTimingRemainsCompact() throws Exception {
        var strip=bake("{\"frametime\":3}");assertEquals(2,strip.frames());assertEquals(3,strip.ticks());assertEquals(4,strip.pixels().getHeight());
    }
    @Test public void customOrderAndUnequalDurationsArePreserved() throws Exception {
        var strip=bake("{\"frames\":[{\"index\":1,\"time\":4},{\"index\":0,\"time\":2}]}");
        assertEquals(3,strip.frames());assertEquals(2,strip.ticks());
        assertEquals(0xff0000ff,strip.pixels().getRGB(0,0));assertEquals(0xff0000ff,strip.pixels().getRGB(0,2));assertEquals(0xffff0000,strip.pixels().getRGB(0,4));
    }
    @Test public void interpolationMatchesVanillaDisplayRgbAndCurrentAlpha() throws Exception {
        var strip=bake("{\"frametime\":2,\"interpolate\":true}");assertEquals(4,strip.frames());assertEquals(1,strip.ticks());
        assertEquals(0xff7f007f,strip.pixels().getRGB(0,2));assertEquals(0xff7f007f,strip.pixels().getRGB(0,6));
    }
    @Test public void horizontalAtlasIsRepacked() throws Exception {
        var image=new BufferedImage(4,2,BufferedImage.TYPE_INT_ARGB);image.setRGB(2,0,0xff123456);
        var strip=TextureAnimation.bake(image,JsonParser.parseString("{\"width\":2,\"height\":2,\"frames\":[1,0]}").getAsJsonObject());
        assertEquals(2,strip.pixels().getWidth());assertEquals(4,strip.pixels().getHeight());assertEquals(0xff123456,strip.pixels().getRGB(0,0));
    }
    @Test public void defaultFrameSizeUsesSmallerDimensionForHorizontalStrips() throws Exception {
        var image=new BufferedImage(4,2,BufferedImage.TYPE_INT_ARGB);image.setRGB(2,0,0xff123456);
        var strip=TextureAnimation.bake(image,JsonParser.parseString("{}").getAsJsonObject());
        assertEquals(2,strip.frames());assertEquals(2,strip.pixels().getWidth());assertEquals(0xff123456,strip.pixels().getRGB(0,2));
    }
    @Test public void rejectsInvalidFramesAndUnboundedExpansion() throws Exception {
        for(String json:new String[]{"{\"frametime\":0}","{\"frames\":[]}","{\"frames\":[2]}","{\"frames\":[0.5]}","{\"frames\":[{\"index\":0,\"time\":-1}]}","{\"width\":3}","{\"frames\":[{\"index\":0,\"time\":32767},{\"index\":1,\"time\":32766}],\"interpolate\":true}"}) {
            try {bake(json);fail(json);} catch(IOException expected) {}
        }
    }
    @Test public void rejectsCoercedInterpolationFlags() throws Exception {
        for(String json:new String[]{"{\"interpolate\":\"true\"}","{\"interpolate\":1}","{\"interpolate\":null}"}) {
            try {bake(json);fail(json);} catch(IOException expected) {}
        }
    }
    @Test public void longUniformInterpolationUsesCompactShaderFrames() throws Exception {
        var image=new BufferedImage(16,64,BufferedImage.TYPE_INT_ARGB);
        var sequence=new com.google.gson.JsonObject();sequence.addProperty("frametime",300);sequence.addProperty("interpolate",true);
        var frames=new com.google.gson.JsonArray();for(int i=0;i<22;i++) frames.add(i%4);sequence.add("frames",frames);
        var strip=TextureAnimation.bake(image,sequence);
        assertTrue(strip.shaderInterpolation());assertEquals(22,strip.frames());assertEquals(300,strip.ticks());assertEquals(352,strip.pixels().getHeight());
    }
}

package dev.tsunami.bridge;

import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.awt.image.BufferedImage;
import javax.imageio.ImageIO;
import com.google.gson.JsonParser;
import org.junit.*;
import org.junit.rules.TemporaryFolder;
import static org.junit.Assert.*;

public class TextureExportTest {
    @Rule public TemporaryFolder temp=new TemporaryFolder();
    private Map<String,byte[]> resources() throws Exception {
        Map<String,byte[]> result=new HashMap<>();
        put(result,"minecraft:blockstates/grass_block.json","{\"variants\":{\"snowy=true\":{\"model\":\"block/snow\"},\"snowy=false\":{\"model\":\"block/grass\"}}}");
        put(result,"minecraft:models/block/base.json","{\"textures\":{\"bottom\":\"block/dirt\"},\"elements\":[{\"faces\":{\"up\":{\"texture\":\"#top\",\"tintindex\":0},\"north\":{\"texture\":\"#side\"},\"down\":{\"texture\":\"#bottom\"}}}]}");
        put(result,"minecraft:models/block/grass.json","{\"parent\":\"block/base\",\"textures\":{\"top\":\"#upper\",\"upper\":\"block/grass_top\",\"side\":\"block/grass_side\"}}");
        var image=new BufferedImage(2,2,BufferedImage.TYPE_INT_ARGB); image.setRGB(0,0,0xffff0000);
        var out=new ByteArrayOutputStream(); ImageIO.write(image,"png",out);
        for(String id:List.of("grass_top","grass_side","dirt")) result.put("minecraft:textures/block/"+id+".png",out.toByteArray());
        return result;
    }
    private void put(Map<String,byte[]> map,String name,String value) { map.put(name,value.getBytes(StandardCharsets.UTF_8)); }
    private TextureExport make(Map<String,byte[]> source) throws Exception { return new TextureExport(source::get,temp.getRoot().toPath().resolve(UUID.randomUUID().toString())); }
    private TextureExport.Block grass() { return new TextureExport.Block("minecraft:grass_block",Map.of("snowy","false")); }
    @Test public void resolvesDefaultVariantParentAndTextureReferences() throws Exception {
        var exporter=make(resources()); var faces=exporter.resolve(grass());
        assertEquals("minecraft:block/grass_top",faces.top().texture()); assertTrue(faces.top().tint());
        assertEquals("minecraft:block/grass_side",faces.side().texture()); assertFalse(faces.side().tint());
        assertEquals("minecraft:block/dirt",faces.bottom().texture());
    }
    @Test public void writesReusableCheckedManifestAndDeduplicatesTextures() throws Exception {
        var exporter=make(resources()); assertTrue(exporter.export(grass())); assertTrue(exporter.export(grass()));
        Path manifest=exporter.finish(); var json=JsonParser.parseString(Files.readString(manifest)).getAsJsonObject();
        assertEquals(1,json.getAsJsonObject("blocks").size()); assertEquals(3,json.getAsJsonObject("textures").size());
        for(var entry:json.getAsJsonObject("textures").entrySet()) {
            var metadata=entry.getValue().getAsJsonObject(); Path png=manifest.getParent().resolve(metadata.get("file").getAsString());
            assertTrue(Files.isRegularFile(png)); assertEquals(2,metadata.get("width").getAsInt());
            assertEquals(64,metadata.get("sha256").getAsString().length());
        }
        try { exporter.finish(); fail("Existing export manifest overwritten"); } catch(FileAlreadyExistsException expected) { }
    }
    @Test public void unsupportedModelsAndCyclesFallBackWithoutHanging() throws Exception {
        var map=resources(); put(map,"minecraft:models/block/base.json","{\"parent\":\"block/grass\"}");
        var exporter=make(map); assertFalse(exporter.export(grass())); assertEquals(1,exporter.skipped());
        put(map,"minecraft:blockstates/grass_block.json","{\"multipart\":[]}"); assertFalse(make(map).export(grass()));
    }
    @Test public void refusesPathTraversalAndOversizedData() throws Exception {
        for(String id:List.of("minecraft:../../secret","minecraft:/absolute","minecraft:block/../../secret","minecraft:block\\secret")) {
            try { TextureExport.id(id); fail(id); } catch(IOException expected) { }
        }
        var map=resources(); map.put("minecraft:models/block/grass.json",new byte[256*1024+1]);
        assertFalse(make(map).export(grass()));
    }
    @Test public void diskOrResourceIoErrorsAreReportedRatherThanUnsupported() throws Exception {
        var exporter=new TextureExport(name->{throw new IOException("Resource pack closed");},temp.getRoot().toPath().resolve("io-failure"));
        try { exporter.export(grass()); fail(); } catch(IOException expected) { assertEquals("Resource pack closed",expected.getMessage()); }
    }
    @Test public void snapshotsAnimatedTextureFirstFrame() throws Exception {
        var map=resources(); var image=new BufferedImage(2,4,BufferedImage.TYPE_INT_ARGB); image.setRGB(0,0,0xffff0000); image.setRGB(0,2,0xff0000ff);
        var bytes=new ByteArrayOutputStream(); ImageIO.write(image,"png",bytes);
        map.put("minecraft:textures/block/grass_top.png",bytes.toByteArray());
        put(map,"minecraft:textures/block/grass_top.png.mcmeta","{\"animation\":{}}");
        var exporter=make(map); assertTrue(exporter.export(grass())); var manifest=exporter.finish();
        var entry=JsonParser.parseString(Files.readString(manifest)).getAsJsonObject().getAsJsonObject("textures").getAsJsonObject("minecraft:block/grass_top");
        assertEquals(2,entry.get("height").getAsInt()); var png=ImageIO.read(manifest.getParent().resolve(entry.get("file").getAsString()).toFile());
        assertEquals(0xffff0000,png.getRGB(0,0));
    }
    @Test public void exportsInheritedParticleSpriteAndVanillaGrassTintException() throws Exception {
        var map=resources();
        put(map,"minecraft:models/block/base.json","{\"textures\":{\"bottom\":\"block/dirt\",\"particle\":\"#bottom\"},\"elements\":[{\"faces\":{\"up\":{\"texture\":\"#top\",\"tintindex\":0},\"north\":{\"texture\":\"#side\"},\"down\":{\"texture\":\"#bottom\"}}}]}");
        var exporter=make(map);
        assertTrue(exporter.export(new TextureExport.Block("minecraft:grass_block",Map.of("snowy","false"),0x91bd59)));
        var particle=JsonParser.parseString(Files.readString(exporter.finish())).getAsJsonObject().getAsJsonObject("blocks")
                .getAsJsonObject("minecraft:grass_block").getAsJsonObject("particle");
        assertEquals("minecraft:block/dirt",particle.get("texture").getAsString());
        assertFalse(particle.get("tint").getAsBoolean()); assertEquals(0xffffff,particle.get("color").getAsInt());
    }
    @Test public void particlesKeepSampledVanillaBiomeColorForOtherBlocks() throws Exception {
        var map=resources();
        put(map,"minecraft:blockstates/oak_leaves.json","{\"variants\":{\"\":{\"model\":\"block/grass\"}}}");
        var exporter=make(map);
        assertTrue(exporter.export(new TextureExport.Block("minecraft:oak_leaves",Map.of(),0x48b518)));
        var particle=JsonParser.parseString(Files.readString(exporter.finish())).getAsJsonObject().getAsJsonObject("blocks")
                .getAsJsonObject("minecraft:oak_leaves").getAsJsonObject("particle");
        assertEquals("minecraft:block/grass_side",particle.get("texture").getAsString());
        assertTrue(particle.get("tint").getAsBoolean()); assertEquals(0x48b518,particle.get("color").getAsInt());
    }
    @Test public void exportsDistinctParticleSpriteEvenWhenNoCubeFaceUsesIt() throws Exception {
        var map=resources();
        put(map,"minecraft:models/block/grass.json","{\"parent\":\"block/base\",\"textures\":{\"top\":\"block/grass_top\",\"side\":\"block/grass_side\",\"particle\":\"block/dust_only\"}}");
        map.put("minecraft:textures/block/dust_only.png",map.get("minecraft:textures/block/dirt.png"));
        var exporter=make(map); assertTrue(exporter.export(grass()));
        var manifest=JsonParser.parseString(Files.readString(exporter.finish())).getAsJsonObject();
        assertEquals(4,manifest.getAsJsonObject("textures").size());
        assertEquals("minecraft:block/dust_only",manifest.getAsJsonObject("blocks").getAsJsonObject("minecraft:grass_block")
                .getAsJsonObject("particle").get("texture").getAsString());
    }
}

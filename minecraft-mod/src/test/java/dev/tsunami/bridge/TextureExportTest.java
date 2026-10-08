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
        put(result,"minecraft:models/block/snow.json","{\"parent\":\"block/grass\"}");
        var image=new BufferedImage(2,2,BufferedImage.TYPE_INT_ARGB); image.setRGB(0,0,0xffff0000);
        var out=new ByteArrayOutputStream(); ImageIO.write(image,"png",out);
        for(String id:List.of("grass_top","grass_side","dirt")) result.put("minecraft:textures/block/"+id+".png",out.toByteArray());
        return result;
    }
    private void put(Map<String,byte[]> map,String name,String value) { map.put(name,value.getBytes(StandardCharsets.UTF_8)); }
    private TextureExport make(Map<String,byte[]> source) throws Exception { return new TextureExport(source::get,temp.getRoot().toPath().resolve(UUID.randomUUID().toString())); }
    private TextureExport.Block grass() { return new TextureExport.Block("minecraft:grass_block",Map.of("snowy","false")); }
    @Test public void fluidExportPreservesAnimationAlphaAndLevelSurface() throws Exception {
        var map=resources();var image=new BufferedImage(2,4,BufferedImage.TYPE_INT_ARGB);
        for(int y=0;y<4;y++) for(int x=0;x<2;x++) image.setRGB(x,y,y<2 ? 0xffff0000 : 0xff0000ff);
        var bytes=new ByteArrayOutputStream();ImageIO.write(image,"png",bytes);
        map.put("minecraft:textures/block/water_still.png",bytes.toByteArray());
        put(map,"minecraft:textures/block/water_still.png.mcmeta","{\"animation\":{\"frametime\":3}}");
        var props=Map.of("level","5");var exporter=make(map);
        assertTrue(exporter.export(new TextureExport.Block("minecraft:water",props,0x3f76e4,List.of(new TextureExport.State(props,List.of(),List.of())),"")));
        var manifest=exporter.finish();var json=JsonParser.parseString(Files.readString(manifest)).getAsJsonObject();
        var metadata=json.getAsJsonObject("textures").getAsJsonObject("minecraft:block/water_still");
        assertEquals(2,metadata.get("animationFrames").getAsInt());assertEquals(3,metadata.get("animationFrameTime").getAsInt());assertEquals("translucent",metadata.get("alphaMode").getAsString());
        var png=ImageIO.read(manifest.getParent().resolve(metadata.get("file").getAsString()).toFile());
        assertEquals(4,png.getHeight());assertEquals(0xb0ff0000,png.getRGB(0,0));assertEquals(0xb00000ff,png.getRGB(0,2));
        var model=json.getAsJsonObject("models").entrySet().iterator().next().getValue().getAsJsonObject();
        assertEquals(16./3,model.getAsJsonArray("elements").get(0).getAsJsonObject().getAsJsonArray("to").get(1).getAsDouble(),1e-9);
        assertEquals("level=5",json.getAsJsonObject("blocks").getAsJsonObject("minecraft:water").get("defaultState").getAsString());
    }
    @Test public void missingDedicatedBlockModelRetainsPhysicalStatesForItemFallback() throws Exception {
        var map=resources();map.put("minecraft:textures/block/stone.png",map.get("minecraft:textures/block/dirt.png"));
        var boxes=List.of(new double[]{.0625,0,.0625,.9375,.875,.9375});
        var a=Map.of("facing","north");var b=Map.of("facing","south");var exporter=make(map);
        assertTrue(exporter.export(new TextureExport.Block("minecraft:chest",a,0xffffff,List.of(new TextureExport.State(a,boxes,boxes),new TextureExport.State(b,boxes,boxes)),"")));
        var json=JsonParser.parseString(Files.readString(exporter.finish())).getAsJsonObject();
        assertTrue(json.getAsJsonObject("blocks").getAsJsonObject("minecraft:chest").get("itemFallback").getAsBoolean());
        assertEquals(2,json.getAsJsonObject("blocks").getAsJsonObject("minecraft:chest").getAsJsonObject("states").size());
        assertEquals(1,json.getAsJsonObject("models").size());
        assertEquals(2,json.getAsJsonObject("blockstates").getAsJsonObject("minecraft:chest").getAsJsonObject("variants").size());
    }
    @Test public void invisibleBarrierKeepsCollisionWithoutPlaceholderCube() throws Exception {
        var map=resources();map.put("minecraft:textures/block/stone.png",map.get("minecraft:textures/block/dirt.png"));
        var boxes=List.of(new double[]{0,0,0,1,1,1});var exporter=make(map);
        assertTrue(exporter.export(new TextureExport.Block("minecraft:barrier",Map.of(),0xffffff,List.of(new TextureExport.State(Map.of(),boxes,boxes)),"")));
        var json=JsonParser.parseString(Files.readString(exporter.finish())).getAsJsonObject();
        var block=json.getAsJsonObject("blocks").getAsJsonObject("minecraft:barrier");
        assertFalse(block.get("itemFallback").getAsBoolean());
        assertEquals(1,block.getAsJsonObject("states").getAsJsonObject("").getAsJsonArray("collision").size());
        assertEquals(0,json.getAsJsonObject("models").entrySet().iterator().next().getValue().getAsJsonObject().getAsJsonArray("elements").size());
    }
    @Test public void resolvesDefaultVariantParentAndTextureReferences() throws Exception {
        var exporter=make(resources()); var faces=exporter.resolve(grass());
        assertEquals("minecraft:block/grass_top",faces.top().texture()); assertTrue(faces.top().tint());
        assertEquals("minecraft:block/grass_side",faces.side().texture()); assertFalse(faces.side().tint());
        assertEquals("minecraft:block/dirt",faces.bottom().texture());
    }
    @Test public void grassRenderTintIsIndependentOfUntintedDirtDust() throws Exception {
        var exporter=make(resources());
        assertTrue(exporter.export(new TextureExport.Block("minecraft:grass_block",Map.of("snowy","false"),0x91bd59,List.of(),"")));
        var block=JsonParser.parseString(Files.readString(exporter.finish())).getAsJsonObject().getAsJsonObject("blocks").getAsJsonObject("minecraft:grass_block");
        assertEquals(0x91bd59,block.getAsJsonObject("renderTints").get("0").getAsInt());
        assertEquals("grass",block.getAsJsonObject("tintSources").get("0").getAsString());
        assertFalse(block.getAsJsonObject("particle").get("tint").getAsBoolean());
        assertEquals(0xffffff,block.getAsJsonObject("particle").get("color").getAsInt());
    }
    @Test public void nativeTintSourcesPreservePetalAndFixedLeafProviders() {
        assertEquals("none",TextureExport.tintSource("minecraft:pink_petals",0));
        assertEquals("grass",TextureExport.tintSource("minecraft:pink_petals",1));
        assertEquals("foliage",TextureExport.tintSource("minecraft:oak_leaves",0));
        assertEquals("dry_foliage",TextureExport.tintSource("minecraft:leaf_litter",0));
        assertEquals("constant",TextureExport.tintSource("minecraft:birch_leaves",0));
    }
    @Test public void coplanarGrassBaseAndOverlaySurviveResourceExport() throws Exception {
        var map=resources();
        put(map,"minecraft:models/block/grass.json","{\"textures\":{\"base\":\"block/dirt\",\"overlay\":\"block/grass_side\"},\"elements\":[{\"from\":[0,0,0],\"to\":[16,16,16],\"faces\":{\"north\":{\"texture\":\"#base\"}}},{\"from\":[0,0,0],\"to\":[16,16,16],\"faces\":{\"north\":{\"texture\":\"#overlay\",\"tintindex\":0}}}]}");
        var exporter=make(map);assertTrue(exporter.export(grass()));
        var elements=JsonParser.parseString(Files.readString(exporter.finish())).getAsJsonObject().getAsJsonObject("models").getAsJsonObject("minecraft:block/grass").getAsJsonArray("elements");
        assertEquals(2,elements.size());
        var base=elements.get(0).getAsJsonObject().getAsJsonObject("faces").getAsJsonObject("north");
        var overlay=elements.get(1).getAsJsonObject().getAsJsonObject("faces").getAsJsonObject("north");
        assertEquals("minecraft:block/dirt",base.get("texture").getAsString());assertFalse(base.has("tintindex"));
        assertEquals("minecraft:block/grass_side",overlay.get("texture").getAsString());assertEquals(0,overlay.get("tintindex").getAsInt());
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
    @Test public void exportsAllVariantModelsAndNativeStateBoxes() throws Exception {
        var exporter=make(resources());
        var state=new TextureExport.State(Map.of("snowy","false"),List.of(new double[]{0,0,0,1,.5,1}),List.of(new double[]{0,0,0,1,.5,1}));
        assertTrue(exporter.export(new TextureExport.Block("minecraft:grass_block",Map.of("snowy","false"),0xffffff,List.of(state),"")));
        var json=JsonParser.parseString(Files.readString(exporter.finish())).getAsJsonObject();
        assertEquals(2,json.get("version").getAsInt());
        assertTrue(json.getAsJsonObject("models").has("minecraft:block/snow"));
        var box=json.getAsJsonObject("blocks").getAsJsonObject("minecraft:grass_block").getAsJsonObject("states")
            .getAsJsonObject("snowy=false").getAsJsonArray("collision").get(0).getAsJsonArray();
        assertEquals(.5,box.get(4).getAsDouble(),0);
    }
    @Test public void resolvesMultipartConditionsAndExportsEveryAppliedModel() throws Exception {
        var map=resources();
        put(map,"minecraft:blockstates/oak_fence.json","{\"multipart\":[{\"apply\":{\"model\":\"block/grass\"}},{\"when\":{\"OR\":[{\"north\":\"true\"},{\"east\":\"true|low\"}]},\"apply\":{\"model\":\"block/snow\",\"y\":90}}]}");
        var exporter=make(map); assertTrue(exporter.export(new TextureExport.Block("minecraft:oak_fence",Map.of("north","true"))));
        var json=JsonParser.parseString(Files.readString(exporter.finish())).getAsJsonObject();
        assertEquals(2,json.getAsJsonObject("models").size());
        assertEquals("minecraft:block/grass",json.getAsJsonObject("blockstates").getAsJsonObject("minecraft:oak_fence")
            .getAsJsonArray("multipart").get(0).getAsJsonObject().getAsJsonObject("apply").get("model").getAsString());
    }
    @Test public void excludedBlocksHaveExplicitReasonAndStateKeysAreSorted() throws Exception {
        var exporter=make(resources());
        assertFalse(exporter.export(new TextureExport.Block("minecraft:oak_sign",Map.of(),0xffffff,List.of(),"sign")));
        var json=JsonParser.parseString(Files.readString(exporter.finish())).getAsJsonObject();
        assertEquals("sign",json.getAsJsonObject("excluded").get("minecraft:oak_sign").getAsString());
        assertEquals("facing=north,half=top",TextureExport.stateKey(Map.of("half","top","facing","north")));
    }
    @Test public void partialAlphaAndBareVanillaTextureVariableArePreserved() throws Exception {
        var map=resources();
        put(map,"minecraft:models/block/grass.json","{\"textures\":{\"all\":\"block/grass_top\"},\"elements\":[{\"from\":[4,0,4],\"to\":[12,8,12],\"faces\":{\"north\":{\"texture\":\"all\"}}}]}");
        var image=new BufferedImage(1,1,BufferedImage.TYPE_INT_ARGB);image.setRGB(0,0,0x80ffffff);
        var out=new ByteArrayOutputStream();ImageIO.write(image,"png",out);map.put("minecraft:textures/block/grass_top.png",out.toByteArray());
        var exporter=make(map);assertTrue(exporter.export(grass()));
        var json=JsonParser.parseString(Files.readString(exporter.finish())).getAsJsonObject();
        assertEquals("translucent",json.getAsJsonObject("textures").getAsJsonObject("minecraft:block/grass_top").get("alphaMode").getAsString());
        assertEquals("minecraft:block/grass_top",json.getAsJsonObject("models").getAsJsonObject("minecraft:block/grass")
            .getAsJsonArray("elements").get(0).getAsJsonObject().getAsJsonObject("faces").getAsJsonObject("north").get("texture").getAsString());
    }
}

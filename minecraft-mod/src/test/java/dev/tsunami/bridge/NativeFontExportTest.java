package dev.tsunami.bridge;

import com.google.gson.*;
import java.awt.image.BufferedImage;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;
import javax.imageio.ImageIO;
import org.junit.*;
import org.junit.rules.TemporaryFolder;
import static org.junit.Assert.*;

public class NativeFontExportTest {
    @Rule public TemporaryFolder temp=new TemporaryFolder();
    private static final class Resources implements NativeFontExport.Resources {
        final Map<String,List<byte[]>> entries=new HashMap<>();
        void add(String id,String text) {add(id,text.getBytes(StandardCharsets.UTF_8));}
        void add(String id,byte[] bytes) {entries.computeIfAbsent(id,key->new ArrayList<>()).add(bytes);}
        public byte[] read(String id) {var values=entries.get(id);return values==null?null:values.getLast();}
        public List<byte[]> all(String id) {return entries.getOrDefault(id,List.of());}
    }
    private static byte[] png(BufferedImage image) throws IOException {var out=new ByteArrayOutputStream();ImageIO.write(image,"png",out);return out.toByteArray();}
    private static JsonObject glyph(JsonObject manifest,int codepoint) {
        for(var entry:manifest.getAsJsonArray("glyphs")) if(entry.getAsJsonObject().get("codepoint").getAsInt()==codepoint) return entry.getAsJsonObject();
        fail("Expected glyph U+"+Integer.toHexString(codepoint));return null;
    }
    private Path output() throws IOException {return temp.newFolder().toPath();}

    @Test public void bitmapMetricsPixelsAndSupplementaryCodepointsArePreserved() throws Exception {
        Resources source=new Resources();source.add("minecraft:font/default.json","{\"providers\":[{\"type\":\"bitmap\",\"file\":\"test:glyph.png\",\"height\":2,\"ascent\":1,\"chars\":[\"A😀\"]}]}");
        var image=new BufferedImage(8,4,BufferedImage.TYPE_INT_ARGB);image.setRGB(2,1,0xffffffff);image.setRGB(6,2,0xff00ff00);source.add("test:textures/glyph.png",png(image));
        Path directory=output();var manifest=NativeFontExport.export(source,directory,Set.of(65,0x1f600),false,false);
        assertTrue(manifest.getAsJsonArray("missing").isEmpty());assertEquals(2,manifest.getAsJsonArray("glyphs").size());
        var a=glyph(manifest,65);assertEquals(3,a.get("advance").getAsFloat(),0);assertEquals(2,a.get("drawWidth").getAsFloat(),0);
        assertEquals(2,a.get("drawHeight").getAsFloat(),0);assertEquals(1,a.get("ascent").getAsFloat(),0);assertEquals(4,a.get("width").getAsInt());assertEquals(4,a.get("height").getAsInt());
        var emoji=glyph(manifest,0x1f600);var atlas=ImageIO.read(directory.resolve("font.png").toFile());
        assertEquals(0xffffffff,atlas.getRGB(a.get("x").getAsInt()+2,a.get("y").getAsInt()+1));
        assertEquals(0xff00ff00,atlas.getRGB(emoji.get("x").getAsInt()+2,emoji.get("y").getAsInt()+2));
        assertEquals(NativeExportData.sha256(directory.resolve("font.png")),manifest.get("sha256").getAsString());
    }

    @Test public void higherPriorityPacksOverrideGlyphsAndFallbackAddsMissingOnes() throws Exception {
        Resources source=new Resources();
        source.add("minecraft:font/default.json","{\"providers\":[{\"type\":\"bitmap\",\"file\":\"test:low.png\",\"height\":1,\"ascent\":1,\"chars\":[\"A\"]},{\"type\":\"space\",\"advances\":{\" \":4}}]}");
        source.add("minecraft:font/default.json","{\"providers\":[{\"type\":\"bitmap\",\"file\":\"test:high.png\",\"height\":1,\"ascent\":1,\"chars\":[\"A\"]}]}");
        var low=new BufferedImage(1,1,BufferedImage.TYPE_INT_ARGB);low.setRGB(0,0,0xffff0000);var high=new BufferedImage(1,1,BufferedImage.TYPE_INT_ARGB);high.setRGB(0,0,0xff0000ff);
        source.add("test:textures/low.png",png(low));source.add("test:textures/high.png",png(high));
        Path directory=output();var manifest=NativeFontExport.export(source,directory,Set.of(65,32),false,false);
        var a=glyph(manifest,65);var atlas=ImageIO.read(directory.resolve("font.png").toFile());
        assertEquals(0xff0000ff,atlas.getRGB(a.get("x").getAsInt(),a.get("y").getAsInt()));
        assertEquals(4,glyph(manifest,32).get("advance").getAsFloat(),0);assertEquals(0,glyph(manifest,32).get("drawWidth").getAsFloat(),0);
    }

    @Test public void providerFiltersUseCapturedUnicodeAndJapaneseSettings() throws Exception {
        Resources source=new Resources();source.add("minecraft:font/default.json","{\"providers\":[{\"type\":\"space\",\"advances\":{\"A\":2},\"filter\":{\"uniform\":false}},{\"type\":\"space\",\"advances\":{\"A\":4},\"filter\":{\"uniform\":true}},{\"type\":\"space\",\"advances\":{\"日\":8},\"filter\":{\"jp\":true}}]}");
        var normal=NativeFontExport.export(source,output(),Set.of(65,0x65e5),false,false);
        assertEquals(2,glyph(normal,65).get("advance").getAsFloat(),0);assertEquals(0x65e5,normal.getAsJsonArray("missing").get(0).getAsInt());
        var japanese=NativeFontExport.export(source,output(),Set.of(65,0x65e5),true,true);
        assertEquals(4,glyph(japanese,65).get("advance").getAsFloat(),0);assertEquals(8,glyph(japanese,0x65e5).get("advance").getAsFloat(),0);assertTrue(japanese.getAsJsonArray("missing").isEmpty());
    }

    @Test public void cyclicReferencesTerminateWithoutDroppingLaterProviders() throws Exception {
        Resources source=new Resources();source.add("minecraft:font/default.json","{\"providers\":[{\"type\":\"reference\",\"id\":\"test:loop\"}]}");
        source.add("test:font/loop.json","{\"providers\":[{\"type\":\"reference\",\"id\":\"minecraft:default\"},{\"type\":\"space\",\"advances\":{\"A\":7}}]}");
        var manifest=NativeFontExport.export(source,output(),Set.of(65),false,false);assertEquals(7,glyph(manifest,65).get("advance").getAsFloat(),0);
    }

    @Test public void unihexTrimsInkAndHonorsJapaneseWidthOverride() throws Exception {
        Resources source=new Resources();source.add("minecraft:font/default.json","{\"providers\":[{\"type\":\"unihex\",\"hex_file\":\"test:font/unifont.zip\",\"size_overrides\":[{\"from\":\"日\",\"to\":\"日\",\"left\":0,\"right\":15}]}]}");
        var bytes=new ByteArrayOutputStream();try(var zip=new ZipOutputStream(bytes)) {zip.putNextEntry(new ZipEntry("glyphs.hex"));zip.write(("0041:"+"18".repeat(16)+"\n65E5:"+"0180".repeat(16)+"\n").getBytes(StandardCharsets.UTF_8));zip.closeEntry();}
        source.add("test:font/unifont.zip",bytes.toByteArray());Path directory=output();var manifest=NativeFontExport.export(source,directory,Set.of(65,0x65e5),false,true);
        var a=glyph(manifest,65);var japanese=glyph(manifest,0x65e5);assertEquals(2,a.get("width").getAsInt());assertEquals(2,a.get("advance").getAsFloat(),0);
        assertEquals(16,japanese.get("width").getAsInt());assertEquals(9,japanese.get("advance").getAsFloat(),0);
        var atlas=ImageIO.read(directory.resolve("font.png").toFile());assertEquals(0xffffffff,atlas.getRGB(a.get("x").getAsInt(),a.get("y").getAsInt()));
        assertEquals(0,atlas.getRGB(japanese.get("x").getAsInt(),japanese.get("y").getAsInt()));
        assertEquals(0xffffffff,atlas.getRGB(japanese.get("x").getAsInt()+7,japanese.get("y").getAsInt()));
    }

    @Test public void unsupportedProvidersReportMissingGlyphsWithoutSilentSubstitution() throws Exception {
        Resources source=new Resources();source.add("minecraft:font/default.json","{\"providers\":[{\"type\":\"ttf\",\"file\":\"test:font/custom.ttf\"}]}");
        var manifest=NativeFontExport.export(source,output(),Set.of(65),false,false);
        assertTrue(manifest.getAsJsonArray("glyphs").isEmpty());assertEquals(65,manifest.getAsJsonArray("missing").get(0).getAsInt());
        assertTrue(manifest.getAsJsonArray("excludedProviders").get(0).getAsString().contains("ttf"));
    }

    @Test public void malformedBitmapRowsAndExcessGlyphsFailExplicitly() throws Exception {
        Resources source=new Resources();source.add("minecraft:font/default.json","{\"providers\":[{\"type\":\"bitmap\",\"file\":\"test:glyph.png\",\"height\":1,\"ascent\":1,\"chars\":[\"AB\",\"A\"]}]}");
        source.add("test:textures/glyph.png",png(new BufferedImage(2,2,BufferedImage.TYPE_INT_ARGB)));
        try {NativeFontExport.export(source,output(),Set.of(65,66),false,false);fail();}catch(IOException expected) {assertTrue(expected.getMessage().contains("row mismatch"));}
        Set<Integer> oversized=new HashSet<>();for(int i=0;i<4097;i++) oversized.add(i);
        try {NativeFontExport.export(source,output(),oversized,false,false);fail();}catch(IOException expected) {assertTrue(expected.getMessage().contains("4096"));}
    }

    @Test public void bitmapSmallerThanCharacterGridIsAnExplicitFailure() throws Exception {
        Resources source=new Resources();source.add("minecraft:font/default.json","{\"providers\":[{\"type\":\"bitmap\",\"file\":\"test:glyph.png\",\"height\":1,\"ascent\":1,\"chars\":[\"AB\"]}]}");
        source.add("test:textures/glyph.png",png(new BufferedImage(1,1,BufferedImage.TYPE_INT_ARGB)));
        try {NativeFontExport.export(source,output(),Set.of(65),false,false);fail();}catch(IOException expected) {assertTrue(expected.getMessage().contains("character grid"));}
    }

    @Test public void oversizedBitmapDimensionsAreRejectedBeforePixelDecode() throws Exception {
        Resources source=new Resources();source.add("minecraft:font/default.json","{\"providers\":[{\"type\":\"bitmap\",\"file\":\"test:glyph.png\",\"height\":1,\"ascent\":1,\"chars\":[\"A\"]}]}");
        // Only the PNG header is valid: a decoder that tries pixel allocation/read
        // before checking dimensions would fail at IDAT instead of this budget check.
        var bytes=new ByteArrayOutputStream();try(var data=new DataOutputStream(bytes)) {
            data.writeLong(0x89504e470d0a1a0aL);data.writeInt(13);data.writeBytes("IHDR");
            var header=new ByteArrayOutputStream();try(var h=new DataOutputStream(header)) {h.writeInt(4096);h.writeInt(4096);h.write(new byte[]{8,6,0,0,0});}
            byte[] payload=header.toByteArray();data.write(payload);var crc=new CRC32();crc.update("IHDR".getBytes(StandardCharsets.US_ASCII));crc.update(payload);data.writeInt((int)crc.getValue());
            data.writeInt(0);data.writeBytes("IEND");data.writeInt(0xae426082);
        }
        source.add("test:textures/glyph.png",bytes.toByteArray());
        try {NativeFontExport.export(source,output(),Set.of(65),false,false);fail();}catch(IOException expected) {assertTrue(expected.getMessage(),expected.getMessage().contains("dimensions exceed budget"));}
    }
}

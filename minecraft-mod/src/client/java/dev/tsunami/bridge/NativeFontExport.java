package dev.tsunami.bridge;

import com.google.gson.*;
import java.awt.image.BufferedImage;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.zip.ZipInputStream;
import javax.imageio.ImageIO;

/** Active-pack bitmap/unihex font bake. TTF and custom providers are reported rather than substituted silently. */
public final class NativeFontExport {
    public interface Resources extends TextureExport.Resources {List<byte[]> all(String id) throws IOException;}
    private record Glyph(BufferedImage image,float advance,float width,float height,float ascent) {}
    private final Resources resources;private final Set<Integer> needed;private final boolean uniform,japanese;
    private final Map<Integer,Glyph> glyphs=new LinkedHashMap<>();private final Set<String> seen=new HashSet<>();private final JsonArray exclusions=new JsonArray();
    private NativeFontExport(Resources source,Set<Integer> needed,boolean uniform,boolean japanese) {resources=source;this.needed=needed;this.uniform=uniform;this.japanese=japanese;}
    public static JsonObject export(Resources source,Path directory,Set<Integer> needed,boolean uniform,boolean japanese) throws IOException {
        if(needed.size()>4096) throw new IOException("Native font unique-glyph limit 4096 exceeded");
        NativeFontExport job=new NativeFontExport(source,needed,uniform,japanese);job.font("minecraft:default",0);
        int count=needed.size(),width=1024,rows=(count+31)/32,height=Math.max(32,rows*32);
        if(height>4096) throw new IOException("Native font atlas limit exceeded");
        BufferedImage atlas=new BufferedImage(width,height,BufferedImage.TYPE_INT_ARGB);JsonArray entries=new JsonArray(),missing=new JsonArray();int index=0;
        for(int codepoint:new TreeSet<>(needed)) {
            Glyph glyph=job.glyphs.get(codepoint);if(glyph==null) {missing.add(codepoint);continue;}
            int x=index%32*32,y=index/32*32;++index;
            if(glyph.image.getWidth()>32 || glyph.image.getHeight()>32) {job.exclusions.add("oversized glyph U+"+Integer.toHexString(codepoint));missing.add(codepoint);continue;}
            for(int py=0;py<glyph.image.getHeight();py++) for(int px=0;px<glyph.image.getWidth();px++) atlas.setRGB(x+px,y+py,glyph.image.getRGB(px,py));
            JsonObject entry=new JsonObject();entry.addProperty("codepoint",codepoint);entry.addProperty("x",x);entry.addProperty("y",y);
            entry.addProperty("width",glyph.image.getWidth());entry.addProperty("height",glyph.image.getHeight());entry.addProperty("advance",glyph.advance);
            entry.addProperty("drawWidth",glyph.width);entry.addProperty("drawHeight",glyph.height);entry.addProperty("ascent",glyph.ascent);entries.add(entry);
        }
        Path output=directory.resolve("font.png");ImageIO.write(atlas,"png",output.toFile());
        JsonObject manifest=new JsonObject();manifest.addProperty("file","font.png");manifest.addProperty("sha256",NativeExportData.sha256(output));manifest.addProperty("width",width);manifest.addProperty("height",height);
        manifest.addProperty("lineHeight",9);manifest.add("glyphs",entries);manifest.add("missing",missing);manifest.add("excludedProviders",job.exclusions);
        manifest.addProperty("source","Active-pack bitmap/unihex providers; nearest sampling; unsupported providers explicitly listed");return manifest;
    }
    private void font(String id,int depth) throws IOException {
        if(depth>16 || !seen.add(id)) return;
        String file=resource(id,"font/",".json");List<byte[]> versions=resources.all(file);
        if(versions.isEmpty()) {exclusions.add("font resource missing: "+id);return;}
        // Higher-priority resource packs' providers precede vanilla fallback providers.
        for(int v=versions.size()-1;v>=0;v--) {
            JsonObject font=JsonParser.parseString(new String(versions.get(v),StandardCharsets.UTF_8)).getAsJsonObject();
            if(!font.has("providers")) continue;
            for(JsonElement value:font.getAsJsonArray("providers")) {
                JsonObject provider=value.getAsJsonObject();if(!active(provider)) continue;
                String type=provider.get("type").getAsString().replace("minecraft:","");
                switch(type) {
                    case "reference" -> font(provider.get("id").getAsString(),depth+1);
                    case "bitmap" -> bitmap(provider);
                    case "unihex" -> unihex(provider);
                    case "space" -> {for(var e:provider.getAsJsonObject("advances").entrySet()) {int cp=e.getKey().codePointAt(0);if(needed.contains(cp)) glyphs.putIfAbsent(cp,new Glyph(new BufferedImage(1,1,BufferedImage.TYPE_INT_ARGB),e.getValue().getAsFloat(),0,0,7));}}
                    default -> exclusions.add(type+" provider in "+id+" is not a portable bitmap font");
                }
            }
        }
    }
    private boolean active(JsonObject provider) {
        if(!provider.has("filter")) return true;
        for(var entry:provider.getAsJsonObject("filter").entrySet()) {
            boolean setting=switch(entry.getKey()) {case "uniform" -> uniform;case "jp" -> japanese;default -> false;};
            if(setting!=entry.getValue().getAsBoolean()) return false;
        }
        return true;
    }
    private void bitmap(JsonObject provider) throws IOException {
        byte[] png=resources.read(resource(provider.get("file").getAsString(),"textures/",""));if(png==null) {exclusions.add("bitmap texture missing: "+provider.get("file"));return;}
        BufferedImage image=decodeBitmap(png);
        JsonArray chars=provider.getAsJsonArray("chars");if(chars.isEmpty()) return;
        int[] first=chars.get(0).getAsString().codePoints().toArray();if(first.length==0) return;
        int cw=image.getWidth()/first.length,ch=image.getHeight()/chars.size();float drawHeight=provider.has("height") ? provider.get("height").getAsFloat() : 8;
        float ascent=provider.get("ascent").getAsFloat();
        if(cw<1 || ch<1) throw new IOException("Font bitmap is smaller than its character grid");
        if(!Float.isFinite(drawHeight) || drawHeight<=0 || !Float.isFinite(ascent)) throw new IOException("Invalid font bitmap metrics");
        float scale=drawHeight/ch;
        for(int row=0;row<chars.size();row++) {
            int[] points=chars.get(row).getAsString().codePoints().toArray();if(points.length!=first.length) throw new IOException("Font bitmap row mismatch");
            for(int col=0;col<points.length;col++) {
                int cp=points[col];if(cp==0 || !needed.contains(cp) || glyphs.containsKey(cp)) continue;
                int right=0;for(int x=cw-1;x>=0;x--) {boolean visible=false;for(int y=0;y<ch;y++) if((image.getRGB(col*cw+x,row*ch+y)>>>24)!=0) {visible=true;break;}if(visible) {right=x+1;break;}}
                int outWidth=Math.max(1,(int)Math.ceil(cw*scale*2)),outHeight=Math.max(1,(int)Math.ceil(drawHeight*2));
                if(outWidth>32 || outHeight>32) {exclusions.add("oversized bitmap glyph: "+cp);continue;}
                BufferedImage glyph=new BufferedImage(outWidth,outHeight,BufferedImage.TYPE_INT_ARGB);
                for(int y=0;y<outHeight;y++) for(int x=0;x<outWidth;x++) glyph.setRGB(x,y,image.getRGB(col*cw+Math.min(cw-1,(int)(x/(scale*2))),row*ch+Math.min(ch-1,(int)(y/(scale*2)))));
                glyphs.put(cp,new Glyph(glyph,Math.round(right*scale)+1,cw*scale,drawHeight,ascent));
            }
        }
    }
    private static BufferedImage decodeBitmap(byte[] png) throws IOException {
        if(png.length>4*1024*1024) throw new IOException("Font bitmap byte budget exceeded");
        // Read dimensions before decompressing pixels, including highly compressed pack images.
        try(var input=new javax.imageio.stream.MemoryCacheImageInputStream(new ByteArrayInputStream(png))) {
            var readers=ImageIO.getImageReaders(input);if(!readers.hasNext()) throw new IOException("Invalid font bitmap");
            var reader=readers.next();
            try {
                reader.setInput(input,true,true);int width=reader.getWidth(0),height=reader.getHeight(0);
                if(width<1 || height<1 || width>4096 || height>4096 || (long)width*height>4_194_304)
                    throw new IOException("Font bitmap dimensions exceed budget");
                return reader.read(0);
            } finally {reader.dispose();}
        }
    }
    private void unihex(JsonObject provider) throws IOException {
        byte[] zip=resources.read(provider.get("hex_file").getAsString());if(zip==null || zip.length>64*1024*1024) {exclusions.add("unihex resource missing/oversized");return;}
        long expanded=0;try(var input=new ZipInputStream(new ByteArrayInputStream(zip))) {
            for(var entry=input.getNextEntry();entry!=null;entry=input.getNextEntry()) {
                if(entry.isDirectory() || !entry.getName().endsWith(".hex")) continue;
                var reader=new BufferedReader(new InputStreamReader(input,StandardCharsets.UTF_8));
                for(String line;(line=reader.readLine())!=null;) {
                    expanded+=line.length()+1;if(expanded>128L*1024*1024) throw new IOException("Unihex expansion limit");
                    int colon=line.indexOf(':');if(colon<1 || colon>6 || line.length()>150) continue;
                    int cp;try {cp=Integer.parseInt(line.substring(0,colon),16);}catch(NumberFormatException invalid) {continue;}
                    if(!needed.contains(cp) || glyphs.containsKey(cp)) continue;
                    String data=line.substring(colon+1).trim();int width=data.length()/4;
                    if(width!=8 && width!=16 && width!=24 && width!=32) continue;
                    int[] rows=new int[16];int left=width,right=-1;
                    try {for(int y=0;y<16;y++) {rows[y]=(int)Long.parseUnsignedLong(data.substring(y*width/4,(y+1)*width/4),16);for(int x=0;x<width;x++) if((rows[y]&(1<<(width-1-x)))!=0) {left=Math.min(left,x);right=Math.max(right,x);}}}catch(NumberFormatException invalid) {continue;}
                    if(provider.has("size_overrides")) for(JsonElement e:provider.getAsJsonArray("size_overrides")) {var o=e.getAsJsonObject();int from=o.get("from").getAsString().codePointAt(0),to=o.get("to").getAsString().codePointAt(0);if(cp>=from && cp<=to) {left=o.get("left").getAsInt();right=o.get("right").getAsInt();break;}}
                    if(right<left) {left=0;right=width-1;}int glyphWidth=right-left+1;
                    if(left<0 || right>=width || glyphWidth>32) continue;
                    BufferedImage glyph=new BufferedImage(glyphWidth,16,BufferedImage.TYPE_INT_ARGB);
                    for(int y=0;y<16;y++) for(int x=0;x<glyphWidth;x++) if((rows[y]&(1<<(width-1-(x+left))))!=0) glyph.setRGB(x,y,0xffffffff);
                    glyphs.put(cp,new Glyph(glyph,(glyphWidth/2f)+1,glyphWidth/2f,8,7));
                }
            }
        }
    }
    private static String resource(String id,String prefix,String suffix) throws IOException {String valid=TextureExport.id(id);int colon=valid.indexOf(':');return valid.substring(0,colon+1)+prefix+valid.substring(colon+1)+suffix;}
}

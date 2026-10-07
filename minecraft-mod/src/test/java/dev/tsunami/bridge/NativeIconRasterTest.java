package dev.tsunami.bridge;

import com.google.gson.*;
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.util.*;
import org.junit.Test;
import static org.junit.Assert.*;

public class NativeIconRasterTest {
    private static JsonObject quad(double depth,int tint,boolean reversed) {
        JsonObject face=new JsonObject();face.addProperty("texture","test:icon");face.addProperty("color",tint);
        JsonArray points=new JsonArray(),uv=new JsonArray();int[] order=reversed?new int[]{3,2,1,0}:new int[]{0,1,2,3};
        double[][] vertices={{-.5,.5,depth},{.5,.5,depth},{.5,-.5,depth},{-.5,-.5,depth}},coords={{0,0},{1,0},{1,1},{0,1}};
        for(int i:order) {points.add(NativeExportData.array(vertices[i]));uv.add(NativeExportData.array(coords[i]));}
        face.add("vertices",points);face.add("uv",uv);return face;
    }
    private static JsonArray faces(JsonObject... entries) {JsonArray result=new JsonArray();for(var entry:entries) result.add(entry);return result;}
    private static BufferedImage solid(int argb) {var image=new BufferedImage(1,1,BufferedImage.TYPE_INT_ARGB);image.setRGB(0,0,argb);return image;}

    @Test public void windingDoesNotDiscardFacesOrChangeUvOrientation() throws Exception {
        var texture=new BufferedImage(2,2,BufferedImage.TYPE_INT_ARGB);texture.setRGB(0,0,0xffff0000);texture.setRGB(1,0,0xff00ff00);texture.setRGB(0,1,0xff0000ff);texture.setRGB(1,1,0xffffffff);
        var a=NativeIconRaster.render(faces(quad(0,0xffffff,false)),Map.of("test:icon",texture),16);
        var b=NativeIconRaster.render(faces(quad(0,0xffffff,true)),Map.of("test:icon",texture),16);
        assertArrayEquals(a.getRGB(0,0,16,16,null,0,16),b.getRGB(0,0,16,16,null,0,16));
        assertEquals(0xffff0000,a.getRGB(2,2));assertEquals(0xff00ff00,a.getRGB(13,2));assertEquals(0xff0000ff,a.getRGB(2,13));assertEquals(0xffffffff,a.getRGB(13,13));
    }

    @Test public void transparentTexelsRemainHolesAndTintMultipliesNativeColor() throws Exception {
        var texture=new BufferedImage(2,1,BufferedImage.TYPE_INT_ARGB);texture.setRGB(0,0,0x00ffffff);texture.setRGB(1,0,0xff80ff40);
        var image=NativeIconRaster.render(faces(quad(0,0x8040ff,false)),Map.of("test:icon",texture),16);
        assertEquals(0,image.getRGB(3,8));assertEquals(0xff404040,image.getRGB(12,8));
    }

    @Test public void aSingleTranslucentQuadHasNoDoubledOpacityDiagonal() throws Exception {
        for(boolean reversed:List.of(false,true)) {
            var image=NativeIconRaster.render(faces(quad(0,0xffffff,reversed)),Map.of("test:icon",solid(0x80ff0000)),16);
            for(int y=0;y<16;y++) for(int x=0;x<16;x++) assertEquals("Coverage at "+x+","+y,0x80ff0000,image.getRGB(x,y));
        }
    }

    @Test public void depthOrderingBlendsSeparateLayersExactlyOnce() throws Exception {
        JsonObject back=quad(-.25,0xffffff,false),front=quad(.25,0xffffff,false);back.addProperty("texture","test:back");front.addProperty("texture","test:front");
        var textures=Map.of("test:back",solid(0xff0000ff),"test:front",solid(0x80ff0000));
        var a=NativeIconRaster.render(faces(front,back),textures,16);var b=NativeIconRaster.render(faces(back,front),textures,16);
        assertArrayEquals(a.getRGB(0,0,16,16,null,0,16),b.getRGB(0,0,16,16,null,0,16));
        assertEquals(0xff80007f,a.getRGB(8,8));assertEquals(a.getRGB(8,8),a.getRGB(9,8));
    }

    @Test public void degenerateAndOffscreenQuadsDoNotWritePixels() throws Exception {
        var line=quad(0,0xffffff,false);for(var vertex:line.getAsJsonArray("vertices")) vertex.getAsJsonArray().set(0,new JsonPrimitive(0));
        var offscreen=quad(0,0xffffff,false);for(var vertex:offscreen.getAsJsonArray("vertices")) vertex.getAsJsonArray().set(0,new JsonPrimitive(20));
        var image=NativeIconRaster.render(faces(line,offscreen),Map.of("test:icon",solid(0xffffffff)),16);
        for(int pixel:image.getRGB(0,0,16,16,null,0,16)) assertEquals(0,pixel);
    }

    @Test public void missingTexturesAndBudgetViolationsFailExplicitly() throws Exception {
        try {NativeIconRaster.render(faces(quad(0,0xffffff,false)),Map.of(),16);fail();}catch(IOException expected) {assertTrue(expected.getMessage().contains("missing"));}
        for(int size:new int[]{15,129}) try {NativeIconRaster.render(new JsonArray(),Map.of(),size);fail();}catch(IOException expected) {}
        JsonArray oversized=new JsonArray();for(int i=0;i<8193;i++) oversized.add(quad(0,0xffffff,false));
        try {NativeIconRaster.render(oversized,Map.of("test:icon",solid(0xffffffff)),16);fail();}catch(IOException expected) {}
    }
}

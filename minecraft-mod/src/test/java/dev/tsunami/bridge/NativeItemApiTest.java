package dev.tsunami.bridge;
import com.google.gson.JsonParser;
import net.minecraft.client.render.command.OrderedRenderCommandQueue;
import net.minecraft.client.render.command.RenderCommandQueue;
import net.minecraft.client.util.math.MatrixStack;
import net.minecraft.client.render.RenderLayer;
import net.minecraft.client.util.math.Vector2f;
import org.junit.Test;
import static org.junit.Assert.*;
public class NativeItemApiTest {
    @Test public void dropExportsNativeGroundDisplayWithExistingHands() {
        assertEquals(java.util.Set.of("firstperson_righthand","firstperson_lefthand","thirdperson_righthand","thirdperson_lefthand","ground","none"),ItemModelExport.exportedContexts());
    }
    @Test public void escrowSerializationHooksExistOnActualServerPlayer() throws Exception {
        Class<?> player=Class.forName("net.minecraft.server.network.ServerPlayerEntity",false,getClass().getClassLoader());
        assertEquals(void.class,player.getDeclaredMethod("writeCustomData",net.minecraft.storage.WriteView.class).getReturnType());
        assertEquals(void.class,player.getDeclaredMethod("readCustomData",net.minecraft.storage.ReadView.class).getReturnType());
    }
    @Test public void requiredMixinFieldsExistInActualMinecraft12111Classes() throws Exception {
        for(String[] field:new String[][]{{"net.minecraft.block.FenceGateBlock","type","net.minecraft.block.WoodType"},
            {"net.minecraft.block.TrapdoorBlock","blockSetType","net.minecraft.block.BlockSetType"},
            {"net.minecraft.block.ButtonBlock","blockSetType","net.minecraft.block.BlockSetType"},
            {"net.minecraft.client.render.RenderLayer","renderSetup","net.minecraft.client.render.RenderSetup"},
            {"net.minecraft.client.render.RenderSetup","textures","java.util.Map"},
            {"net.minecraft.client.render.RenderSetup$TextureSpec","location","net.minecraft.util.Identifier"},
            {"net.minecraft.client.texture.SpriteContents","image","net.minecraft.client.texture.NativeImage"}}) {
            var cls=Class.forName(field[0],false,getClass().getClassLoader());
            assertEquals(field[0]+"."+field[1],field[2],cls.getDeclaredField(field[1]).getType().getName());
        }
    }
    @Test public void nativePackedTextureCoordinatesPreserveAsymmetricValues() {
        long uv=Vector2f.toLong(.3125f,.875f);assertEquals(.3125f,Vector2f.getX(uv),0);assertEquals(.875f,Vector2f.getY(uv),0);
    }
    @Test public void itemComponentKeyIsIndependentOfObjectFieldOrder() {
        var a=JsonParser.parseString("{\"z\":1,\"a\":{\"b\":2,\"a\":3}}");var b=JsonParser.parseString("{\"a\":{\"a\":3,\"b\":2},\"z\":1}");
        assertEquals(ItemModelExport.canonicalJson(a).toString(),ItemModelExport.canonicalJson(b).toString());
        assertNotEquals(ItemModelExport.canonicalJson(a).toString(),ItemModelExport.canonicalJson(JsonParser.parseString("{\"a\":2}")).toString());
    }
    interface RenamedQueue {
        void method_123(MatrixStack matrices, RenderLayer layer, OrderedRenderCommandQueue.Custom command);
        RenderCommandQueue method_456(int order);
    }
    @Test public void commandDispatchSurvivesProductionMethodRenaming() {
        var renamed=RenamedQueue.class.getDeclaredMethods();
        for(var method:renamed) assertEquals(method.getParameterCount()==1 ? "batch" : "custom",ItemModelExport.commandKind(method));
        var kinds=new java.util.HashSet<String>();
        for(var method:OrderedRenderCommandQueue.class.getMethods()) if(!method.isDefault()) kinds.add(ItemModelExport.commandKind(method));
        assertTrue(kinds.containsAll(java.util.Set.of("batch","item","model","part","custom")));
    }
    @Test public void compressedExportAcceptsPayloadLargerThanOld64MiBLimit() throws Exception {
        var directory=java.nio.file.Files.createTempDirectory("bridge-large-item-");
        try {
            var manifest=new com.google.gson.JsonObject();manifest.addProperty("kind","items");manifest.addProperty("version",1);
            var padding=new com.google.gson.JsonArray();var chunk=new com.google.gson.JsonPrimitive("a".repeat(1024*1024));
            for(int i=0;i<65;i++) padding.add(chunk);manifest.add("padding",padding);
            var path=ItemModelExport.writeManifest(directory,manifest,256L*1024*1024);
            var envelope=JsonParser.parseString(java.nio.file.Files.readString(path)).getAsJsonObject();
            assertEquals(2,envelope.get("version").getAsInt());assertTrue(envelope.get("uncompressedBytes").getAsLong()>64L*1024*1024);
            assertTrue(java.nio.file.Files.size(directory.resolve("items.json.gz"))<1024*1024);
            long count=0;try(var stream=new java.util.zip.GZIPInputStream(java.nio.file.Files.newInputStream(directory.resolve("items.json.gz")))) {
                byte[] buffer=new byte[8192];int n;while((n=stream.read(buffer))>=0) count+=n;
            }
            assertEquals(envelope.get("uncompressedBytes").getAsLong(),count);
        } finally {try(var paths=java.nio.file.Files.list(directory)) {for(var path:paths.toList()) java.nio.file.Files.delete(path);}java.nio.file.Files.delete(directory);}
    }
    @Test public void oversizedExportNeverPublishesCompletedManifest() throws Exception {
        var directory=java.nio.file.Files.createTempDirectory("bridge-item-limit-");
        try {
            var manifest=new com.google.gson.JsonObject();manifest.addProperty("padding","a".repeat(200));
            assertThrows(java.io.IOException.class,()->ItemModelExport.writeManifest(directory,manifest,32));
            assertFalse(java.nio.file.Files.exists(directory.resolve("manifest.json")));
        } finally {try(var paths=java.nio.file.Files.list(directory)) {for(var path:paths.toList()) java.nio.file.Files.delete(path);}java.nio.file.Files.delete(directory);}
    }
}

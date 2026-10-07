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
}

package dev.tsunami.bridge;

import com.google.gson.*;
import dev.tsunami.bridge.mixin.*;
import java.io.IOException;
import java.lang.reflect.*;
import java.util.*;
import net.minecraft.client.model.Model;
import net.minecraft.client.model.ModelPart;
import net.minecraft.client.render.RenderLayer;
import net.minecraft.client.render.command.OrderedRenderCommandQueue;
import net.minecraft.client.render.entity.LivingEntityRenderer;
import net.minecraft.client.render.entity.feature.FeatureRenderer;
import net.minecraft.client.render.entity.state.LivingEntityRenderState;
import net.minecraft.client.util.math.MatrixStack;
import org.joml.Matrix4f;
import org.joml.Vector3f;
import org.joml.Quaternionf;

/** Runs original feature renderers against a recording queue. Clothing, collars,
 * saddles, armor and eyes use their actual visible model parts and texture bindings. */
final class MobFeatureCapture implements InvocationHandler {
    final JsonArray parts=new JsonArray(),transforms=new JsonArray(),layers=new JsonArray();
    private OrderedRenderCommandQueue queue;
    static MobFeatureCapture capture(LivingEntityRenderer<?,?,?> renderer,LivingEntityRenderState state) throws IOException {
        var capture=new MobFeatureCapture();
        try {
            for(Object value:((LivingEntityMobAccessor)renderer).bridgeFeatures()) {
                @SuppressWarnings("rawtypes") FeatureRenderer feature=(FeatureRenderer)value;
                feature.render(new MatrixStack(),capture.queue(),0xf000f0,state,0,0);
            }
        } catch(RuntimeException error) {throw new IOException("Cannot record mob feature layer",error);}
        return capture;
    }
    static void separateOverlay(JsonObject part) {
        var quads=part.getAsJsonArray("quads");if(quads.isEmpty()) return;
        double[] min={Double.POSITIVE_INFINITY,Double.POSITIVE_INFINITY,Double.POSITIVE_INFINITY},max={Double.NEGATIVE_INFINITY,Double.NEGATIVE_INFINITY,Double.NEGATIVE_INFINITY};
        for(var quad:quads) for(var value:quad.getAsJsonArray()) for(int i=0;i<3;i++) {double p=value.getAsJsonArray().get(i).getAsDouble();min[i]=Math.min(min[i],p);max[i]=Math.max(max[i],p);}
        for(var value:quads) {var quad=value.getAsJsonArray();double[][] p=new double[3][3];for(int v=0;v<3;v++) for(int i=0;i<3;i++) p[v][i]=quad.get(v).getAsJsonArray().get(i).getAsDouble();
            double[] a=new double[3],b=new double[3],center=new double[3];for(int i=0;i<3;i++) {a[i]=p[1][i]-p[0][i];b[i]=p[2][i]-p[0][i];center[i]=(p[0][i]+p[2][i]-min[i]-max[i])/2;}
            double[] n={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};double length=Math.sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);if(length<1e-10) continue;
            double sign=n[0]*center[0]+n[1]*center[1]+n[2]*center[2]<0 ? -1 : 1;
            for(var vertex:quad) for(int i=0;i<3;i++) {var coords=vertex.getAsJsonArray();coords.set(i,new JsonPrimitive(coords.get(i).getAsDouble()+n[i]/length*sign*.002));}
        }
    }
    private OrderedRenderCommandQueue queue() {
        if(queue==null) queue=(OrderedRenderCommandQueue)Proxy.newProxyInstance(getClass().getClassLoader(),new Class<?>[]{OrderedRenderCommandQueue.class},this);
        return queue;
    }
    @Override @SuppressWarnings({"rawtypes","unchecked"}) public Object invoke(Object proxy,Method method,Object[] args) throws Throwable {
        if(method.getDeclaringClass()==Object.class) return switch(method.getName()) {case "toString" -> "UEBridgeMobFeatures";case "hashCode" -> System.identityHashCode(this);case "equals" -> proxy==args[0];default -> null;};
        if(method.isDefault()) return InvocationHandler.invokeDefault(proxy,method,args);
        switch(ItemModelExport.commandKind(method)) {
            case "batch":return queue();
            case "model": {
                Model model=(Model)args[0];model.setAngles(args[1]);
                add(model.getRootPart(),(MatrixStack)args[2],(RenderLayer)args[3],(int)args[6]);return null;
            }
            case "part":add((ModelPart)args[0],(MatrixStack)args[1],(RenderLayer)args[2],(int)args[8]);return null;
            // Held items and non-model beam/label effects have separate UE renderers.
            default:return null;
        }
    }
    private void add(ModelPart root,MatrixStack matrices,RenderLayer layer,int color) throws IOException {
        var setup=((ItemLayerTexturesAccessor)layer).bridgeSetup();
        Object spec=((RenderTexturesAccessor)(Object)setup).bridgeTextures().get("Sampler0");
        if(spec==null) return;
        String texture=((ItemTextureSpecAccessor)spec).bridgeLocation().toString();
        // Glint is a texture transform, not another opaque mesh layer.
        if(texture.contains("enchanted") || texture.contains("glint")) return;
        int first=parts.size();
        JsonObject parent=new JsonObject();parent.addProperty("name","feature."+layers.size()+".matrix");parent.addProperty("parent",-1);parent.add("quads",new JsonArray());
        Matrix4f matrix=matrices.peek().getPositionMatrix();Vector3f position=matrix.getTranslation(new Vector3f()),scale=matrix.getScale(new Vector3f());
        Vector3f rotation=matrix.getUnnormalizedRotation(new Quaternionf()).getEulerAnglesZYX(new Vector3f());
        JsonArray transform=new JsonArray();for(float v:new float[]{position.x*16,position.y*16,position.z*16,rotation.x,rotation.y,rotation.z,scale.x,scale.y,scale.z}) transform.add(v);
        parent.add("transform",transform);parts.add(parent);transforms.add(transform.deepCopy());
        List<ModelPart> nodes=new ArrayList<>();JsonArray captured=new JsonArray();
        MobModelExport.capturePart(root,"feature."+layers.size()+".root",-1,captured,nodes,true);
        for(var value:captured) {
            JsonObject part=value.getAsJsonObject();int parentIndex=part.get("parent").getAsInt();part.addProperty("parent",parentIndex<0 ? first : first+1+parentIndex);
            parts.add(part);transforms.add(part.getAsJsonArray("transform").deepCopy());
        }
        JsonObject entry=new JsonObject();entry.addProperty("texture",texture);entry.addProperty("color",color&0xffffff);entry.addProperty("first",first+1);entry.addProperty("end",parts.size());layers.add(entry);
        if(parts.size()>MobModelExport.MAX_PARTS) throw new IOException("Feature part limit exceeded");
    }
}

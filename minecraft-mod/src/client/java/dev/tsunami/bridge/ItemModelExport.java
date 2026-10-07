package dev.tsunami.bridge;

import com.google.gson.*;
import dev.tsunami.bridge.mixin.*;
import java.io.IOException;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.model.Model;
import net.minecraft.client.model.ModelPart;
import net.minecraft.client.render.*;
import net.minecraft.client.render.command.OrderedRenderCommandQueue;
import net.minecraft.client.render.command.RenderCommandQueue;
import net.minecraft.client.render.item.ItemRenderState;
import net.minecraft.client.render.model.BakedQuad;
import net.minecraft.client.texture.*;
import net.minecraft.client.util.math.MatrixStack;
import net.minecraft.item.*;
import net.minecraft.registry.Registries;
import net.minecraft.util.Identifier;
import org.joml.Vector3f;

/** Captures the native resolved item draw commands, including generated sprite thickness,
 * resource-pack models, layer tints and each hand's display transform. Local assets only. */
public final class ItemModelExport {
    private static final ItemDisplayContext[] CONTEXTS={ItemDisplayContext.FIRST_PERSON_RIGHT_HAND,ItemDisplayContext.FIRST_PERSON_LEFT_HAND,
        ItemDisplayContext.THIRD_PERSON_RIGHT_HAND,ItemDisplayContext.THIRD_PERSON_LEFT_HAND};
    private ItemModelExport() {}
    public static Path export(MinecraftClient client,Path root) throws IOException {
        if(!client.isOnThread() || client.player==null || client.world==null) throw new IOException("Enter a world first");
        Files.createDirectories(root);
        Path dir=root.resolve("items-"+java.time.LocalDateTime.now().toString().replace(':','-')+"-"+UUID.randomUUID().toString().substring(0,8));
        Files.createDirectory(dir);Files.createDirectory(dir.resolve("textures"));
        JsonObject manifest=new JsonObject(),models=new JsonObject(),textures=new JsonObject(),excluded=new JsonObject();
        manifest.addProperty("kind","items");manifest.addProperty("version",1);
        long[] written={0};Map<String,String> cache=new HashMap<>();int count=0;
        for(Item item:Registries.ITEM) {
            if(++count>4096) throw new IOException("Item registry limit exceeded");
            ItemStack stack=item.getDefaultStack();if(stack.isEmpty()) continue;
            String id=modelKey(client,stack);
            try {
                JsonObject contexts=new JsonObject();
                for(ItemDisplayContext context:CONTEXTS) {
                    Capture capture=new Capture(client,dir,textures,written,cache);
                    ItemRenderState state=new ItemRenderState();
                    client.getItemModelManager().updateForLivingEntity(state,stack,context,client.player);
                    if(state.isEmpty()) throw new IOException("Native item model is empty");
                    state.render(new MatrixStack(),capture.queue(),0xf000f0,0,0);
                    if(capture.faces.isEmpty()) throw new IOException("Native renderer emitted no geometry");
                    contexts.add(context.asString(),capture.faces);
                }
                models.add(id,contexts);
            } catch(IOException | RuntimeException error) {excluded.addProperty(id,error.getMessage()==null ? error.getClass().getSimpleName() : error.getMessage());}
        }
        // Actual hotbar/offhand stacks preserve component-driven tints/models for the selected items.
        // These override only their own ID in this local snapshot, not the Minecraft inventory.
        for(int slot=0;slot<9;slot++) captureStack(client,client.player.getInventory().getStack(slot),dir,textures,written,cache,models,excluded);
        captureStack(client,client.player.getOffHandStack(),dir,textures,written,cache,models,excluded);
        manifest.add("items",models);manifest.add("textures",textures);manifest.add("excluded",excluded);
        manifest.addProperty("snapshot","Resolved default stacks and current hotbar/offhand; changing components, animated textures, glint and use-state animation require a fresh export or future live model transfer.");
        Path path=dir.resolve("manifest.json");byte[] data=new Gson().toJson(manifest).getBytes(java.nio.charset.StandardCharsets.UTF_8);
        if(data.length>64*1024*1024) throw new IOException("Item manifest exceeds 64 MiB");
        Files.write(path,data,StandardOpenOption.CREATE_NEW);return path;
    }
    private static void captureStack(MinecraftClient client,ItemStack stack,Path dir,JsonObject textures,long[] written,Map<String,String> cache,JsonObject models,JsonObject excluded) throws IOException {
        if(stack.isEmpty()) return;String id=modelKey(client,stack);
        try {
            JsonObject contexts=new JsonObject();
            for(ItemDisplayContext context:CONTEXTS) {
                Capture capture=new Capture(client,dir,textures,written,cache);ItemRenderState state=new ItemRenderState();
                client.getItemModelManager().updateForLivingEntity(state,stack,context,client.player);
                state.render(new MatrixStack(),capture.queue(),0xf000f0,0,0);
                if(capture.faces.isEmpty()) throw new IOException("No geometry for selected stack");contexts.add(context.asString(),capture.faces);
            }
            models.add(id,contexts);excluded.remove(id);
        } catch(RuntimeException | IOException error) {excluded.addProperty(id,"selected stack: "+error.getMessage());}
    }
    public static String modelKey(MinecraftClient client,ItemStack stack) {
        if(stack.isEmpty()) return "";
        var ops=net.minecraft.registry.RegistryOps.of(com.mojang.serialization.JsonOps.INSTANCE,client.world.getRegistryManager());
        var encoded=ItemStack.CODEC.encodeStart(ops,stack.copyWithCount(1)).getOrThrow();
        String canonical=canonicalJson(encoded).toString();
        return Registries.ITEM.getId(stack.getItem())+"@"+MobModelExport.sha256(canonical.getBytes(java.nio.charset.StandardCharsets.UTF_8));
    }
    static JsonElement canonicalJson(JsonElement value) {
        if(value.isJsonObject()) {JsonObject out=new JsonObject();value.getAsJsonObject().entrySet().stream().sorted(Map.Entry.comparingByKey()).forEach(e->out.add(e.getKey(),canonicalJson(e.getValue())));return out;}
        if(value.isJsonArray()) {JsonArray out=new JsonArray();value.getAsJsonArray().forEach(e->out.add(canonicalJson(e)));return out;}
        return value.deepCopy();
    }
    // Fabric production remaps Minecraft method names. Dispatch on the remapped
    // parameter classes instead of development-only strings such as submitItem.
    static String commandKind(Method method) {
        Class<?>[] p=method.getParameterTypes();
        if(p.length==1 && p[0]==int.class && RenderCommandQueue.class.isAssignableFrom(method.getReturnType())) return "batch";
        if(p.length==9 && p[0]==MatrixStack.class && p[1]==ItemDisplayContext.class && p[5]==int[].class && p[6]==List.class) return "item";
        if(p.length==10 && p[0]==Model.class && p[2]==MatrixStack.class && p[7]==Sprite.class) return "model";
        if(p.length==11 && p[0]==ModelPart.class && p[1]==MatrixStack.class && p[5]==Sprite.class) return "part";
        if(p.length==3 && p[0]==MatrixStack.class && p[1]==RenderLayer.class && p[2]==OrderedRenderCommandQueue.Custom.class) return "custom";
        return "unsupported";
    }
    private static final class Capture implements InvocationHandler {
        final MinecraftClient client;final Path directory;final JsonObject textures;final long[] written;
        final Map<String,String> cache;final JsonArray faces=new JsonArray();private OrderedRenderCommandQueue queue;
        Capture(MinecraftClient c,Path d,JsonObject t,long[] w,Map<String,String> saved) {client=c;directory=d;textures=t;written=w;cache=saved;}
        OrderedRenderCommandQueue queue() {
            if(queue==null) queue=(OrderedRenderCommandQueue)Proxy.newProxyInstance(ItemModelExport.class.getClassLoader(),new Class<?>[]{OrderedRenderCommandQueue.class},this);
            return queue;
        }
        @Override @SuppressWarnings({"rawtypes","unchecked"}) public Object invoke(Object proxy,Method method,Object[] args) throws Throwable {
            if(method.getDeclaringClass()==Object.class) return switch(method.getName()) {case "toString" -> "UEBridgeItemCapture";case "hashCode" -> System.identityHashCode(this);case "equals" -> proxy==args[0];default -> null;};
            if(method.isDefault()) return InvocationHandler.invokeDefault(proxy,method,args);
            switch(commandKind(method)) {
                case "batch": return queue();
                case "item": {
                    MatrixStack matrices=(MatrixStack)args[0];int[] tints=(int[])args[5];
                    for(BakedQuad quad:(List<BakedQuad>)args[6]) {
                        Sprite sprite=quad.sprite();String texture=texture(sprite,null);
                        int color=quad.hasTint() && quad.tintIndex()<tints.length && quad.tintIndex()>=0 ? tints[quad.tintIndex()] : -1;
                        JsonArray vertices=new JsonArray(),uv=new JsonArray();
                        for(int i=0;i<4;i++) {
                            Vector3f v=new Vector3f(quad.getPosition(i));matrices.peek().getPositionMatrix().transformPosition(v);vertices.add(vector(v.x,v.y,v.z));
                            long bits=quad.getTexcoords(i);float u=net.minecraft.client.util.math.Vector2f.getX(bits),vcoord=net.minecraft.client.util.math.Vector2f.getY(bits);
                            uv.add(vector((u-sprite.getMinU())/(sprite.getMaxU()-sprite.getMinU()),(vcoord-sprite.getMinV())/(sprite.getMaxV()-sprite.getMinV())));
                        }
                        add(texture,color,vertices,uv);
                    }
                    return null;
                }
                case "model": {
                    Model model=(Model)args[0];model.setAngles(args[1]);Sprite sprite=(Sprite)args[7];
                    Collector consumer=new Collector(texture(sprite,(RenderLayer)args[3]));
                    model.render((MatrixStack)args[2],consumer,(int)args[4],(int)args[5],(int)args[6]);consumer.finish();return null;
                }
                case "part": {
                    Collector consumer=new Collector(texture((Sprite)args[5],(RenderLayer)args[2]));
                    ((ModelPart)args[0]).render((MatrixStack)args[1],consumer,(int)args[3],(int)args[4],(int)args[8]);consumer.finish();return null;
                }
                case "custom": {
                    if(args.length!=3) throw new IOException("Layered custom renderer is not exportable");
                    Collector consumer=new Collector(texture(null,(RenderLayer)args[1]));
                    ((OrderedRenderCommandQueue.Custom)args[2]).render(((MatrixStack)args[0]).peek(),consumer);consumer.finish();return null;
                }
                default: throw new IOException("Unsupported native item draw command: "+method.getName());
            }
        }
        String texture(Sprite sprite,RenderLayer layer) throws IOException {
            NativeImage original=null;boolean owned=false;String cacheKey=null;
            if(sprite!=null) {cacheKey="sprite:"+sprite.getContents().getId();if(cache.containsKey(cacheKey)) return cache.get(cacheKey);original=((SpriteImageAccessor)(Object)sprite.getContents()).bridgeImage();}
            else {
                var setup=((ItemLayerTexturesAccessor)layer).bridgeSetup();
                Object spec=((RenderTexturesAccessor)(Object)setup).bridgeTextures().get("Sampler0");
                if(spec==null) throw new IOException("No item texture binding");
                Identifier id=((ItemTextureSpecAccessor)spec).bridgeLocation();
                cacheKey="texture:"+id;if(cache.containsKey(cacheKey)) return cache.get(cacheKey);
                var resource=client.getResourceManager().getResource(id);
                if(resource.isPresent()) {try(var in=resource.get().getInputStream()) {original=NativeImage.read(in);owned=true;}}
                else if(client.getTextureManager().getTexture(id) instanceof NativeImageBackedTexture dynamic) original=dynamic.getImage();
                if(original==null) throw new IOException("Missing local item texture: "+id);
            }
            try {
                int width=sprite==null ? original.getWidth() : sprite.getContents().getWidth();
                int height=sprite==null ? original.getHeight() : sprite.getContents().getHeight();
                if(width<1 || height<1 || width>2048 || height>2048) throw new IOException("Item texture size limit");
                Path temporary=directory.resolve("texture.tmp.png");
                try(NativeImage copy=new NativeImage(width,height,false)) {
                    for(int y=0;y<height;y++) for(int x=0;x<width;x++) copy.setColorArgb(x,y,original.getColorArgb(x,y));
                    copy.writeTo(temporary);
                }
                byte[] png=Files.readAllBytes(temporary);Files.delete(temporary);String key=MobModelExport.sha256(png);
                if(!textures.has(key)) {
                    written[0]+=png.length;if(written[0]>256L*1024*1024) throw new IOException("Item texture budget exceeded");
                    String file="textures/"+key+".png";Files.write(directory.resolve(file),png,StandardOpenOption.CREATE_NEW);
                    JsonObject entry=new JsonObject();entry.addProperty("file",file);entry.addProperty("sha256",key);entry.addProperty("width",width);entry.addProperty("height",height);textures.add(key,entry);
                }
                cache.put(cacheKey,key);return key;
            } finally {if(owned && original!=null) original.close();}
        }
        void add(String texture,int color,JsonArray vertices,JsonArray uv) throws IOException {
            if(faces.size()>=8192) throw new IOException("Item face limit");
            JsonObject face=new JsonObject();face.addProperty("texture",texture);face.addProperty("color",color & 0xffffff);face.add("vertices",vertices);face.add("uv",uv);faces.add(face);
        }
        final class Collector implements VertexConsumer {
            final String texture;JsonArray vertices=new JsonArray(),uv=new JsonArray();float x,y,z,u,v;int color=-1;boolean pending;
            Collector(String t) {texture=t;}
            private void commit() {
                if(!pending) return;vertices.add(vector(x,y,z));uv.add(vector(u,v));pending=false;
                if(vertices.size()==4) {try {add(texture,color,vertices,uv);} catch(IOException e){throw new IllegalStateException(e);}vertices=new JsonArray();uv=new JsonArray();}
            }
            void finish() {commit();if(!vertices.isEmpty()) throw new IllegalStateException("Native renderer emitted incomplete quads");}
            public VertexConsumer vertex(float a,float b,float c) {commit();x=a;y=b;z=c;pending=true;return this;}
            public VertexConsumer color(int r,int g,int b,int a) {color=(r<<16)|(g<<8)|b;return this;}
            public VertexConsumer color(int c) {color=c;return this;}
            public VertexConsumer texture(float a,float b) {u=a;v=b;return this;}
            public VertexConsumer overlay(int a,int b) {return this;}
            public VertexConsumer light(int a,int b) {return this;}
            public VertexConsumer normal(float a,float b,float c) {return this;}
            public VertexConsumer lineWidth(float width) {return this;}
        }
    }
    private static JsonArray vector(double... values) {JsonArray result=new JsonArray();for(double value:values) {if(!Double.isFinite(value) || Math.abs(value)>4096) throw new IllegalArgumentException("Invalid native item vertex");result.add((float)value);}return result;}
}

package dev.tsunami.bridge;

import com.google.gson.*;
import java.awt.*;
import java.awt.image.BufferedImage;
import java.io.*;
import java.nio.file.*;
import javax.imageio.ImageIO;
import net.minecraft.component.DataComponentTypes;
import net.minecraft.item.ItemStack;
import net.minecraft.resource.ResourceManager;
import net.minecraft.util.Identifier;

/** Active equipment asset layers, including default leather dye and overlay. */
final class NativeEquipmentTextures {
    static JsonObject capture(ResourceManager manager,ItemStack stack,Path directory) throws IOException {
        var component=stack.get(DataComponentTypes.EQUIPPABLE);if(component==null || component.assetId().isEmpty()) return null;
        String type=component.slot()==net.minecraft.entity.EquipmentSlot.LEGS ? "humanoid_leggings" : "humanoid";
        Identifier id=component.assetId().get().getValue();var definition=manager.getResource(Identifier.of(id.getNamespace(),"equipment/"+id.getPath()+".json"));if(definition.isEmpty()) return null;
        JsonObject json;try(var input=definition.get().getInputStream()) {byte[] bytes=input.readNBytes(65537);if(bytes.length>65536) throw new IOException("Equipment definition budget");json=JsonParser.parseString(new String(bytes,java.nio.charset.StandardCharsets.UTF_8)).getAsJsonObject();}
        if(!json.has("layers") || !json.getAsJsonObject("layers").has(type)) return null;
        var layers=json.getAsJsonObject("layers").getAsJsonArray(type);if(layers.size()>16) throw new IOException("Equipment layer budget");
        BufferedImage result=null;
        for(var value:layers) {
            var layer=value.getAsJsonObject();Identifier texture=Identifier.of(layer.get("texture").getAsString());
            var resource=manager.getResource(Identifier.of(texture.getNamespace(),"textures/entity/equipment/"+type+"/"+texture.getPath()+".png")).orElseThrow(()->new IOException("Equipment texture missing"));
            byte[] bytes;try(var input=resource.getInputStream()) {bytes=input.readNBytes(4*1024*1024+1);if(bytes.length>4*1024*1024) throw new IOException("Equipment PNG budget");}
            BufferedImage image=NativeUiExport.decodePng(bytes);
            if(result==null) result=new BufferedImage(image.getWidth(),image.getHeight(),BufferedImage.TYPE_INT_ARGB);
            if(image.getWidth()!=result.getWidth() || image.getHeight()!=result.getHeight()) throw new IOException("Equipment layers have different dimensions");
            int tint=0xffffff;
            if(layer.has("dyeable")) {
                var dyed=stack.get(DataComponentTypes.DYED_COLOR);var dye=layer.getAsJsonObject("dyeable");
                if(dyed!=null) tint=dyed.rgb();else if(dye.has("color_when_undyed")) tint=dye.get("color_when_undyed").getAsInt();else continue;
            }
            BufferedImage tinted=new BufferedImage(image.getWidth(),image.getHeight(),BufferedImage.TYPE_INT_ARGB);
            for(int y=0;y<image.getHeight();y++) for(int x=0;x<image.getWidth();x++) tinted.setRGB(x,y,MobTextureAtlas.tint(image.getRGB(x,y),tint));
            Graphics2D graphics=result.createGraphics();try {graphics.setComposite(AlphaComposite.SrcOver);graphics.drawImage(tinted,0,0,null);}finally {graphics.dispose();}
        }
        if(result==null) return null;
        ByteArrayOutputStream output=new ByteArrayOutputStream();ImageIO.write(result,"png",output);byte[] bytes=output.toByteArray();String hash=MobModelExport.sha256(bytes),file="sprites/"+hash+".png";
        if(!Files.exists(directory.resolve(file))) Files.write(directory.resolve(file),bytes,StandardOpenOption.CREATE_NEW);
        JsonObject metadata=new JsonObject();metadata.addProperty("file",file);metadata.addProperty("sha256",hash);metadata.addProperty("width",result.getWidth());metadata.addProperty("height",result.getHeight());return metadata;
    }
}

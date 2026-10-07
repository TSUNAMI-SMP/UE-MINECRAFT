package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import dev.tsunami.bridge.mixin.LightmapStateAccessor;
import net.minecraft.block.BlockState;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.render.GameRenderer;
import net.minecraft.entity.effect.StatusEffects;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.BlockRenderView;
import net.minecraft.world.LightType;
import net.minecraft.world.attribute.EnvironmentAttributes;

/** Read-only active-world lighting snapshot, using 1.21.11 environment attributes. */
public final class VanillaLightingState {
    private VanillaLightingState() {}
    public record Light(int sky,int block,int emission,int opacity) {}
    public static Light light(BlockRenderView world,BlockPos pos,BlockState state) {
        return new Light(world.getLightLevel(LightType.SKY,pos),world.getLightLevel(LightType.BLOCK,pos),state.getLuminance(),state.getOpacity());
    }
    public static void write(JsonObject packet,MinecraftClient client,Vec3d sourceCameraPosition) {
        if(client.world==null || client.player==null) return;
        var world=client.world;var attributes=world.getEnvironmentAttributes();
        var lightmap=(LightmapStateAccessor)client.gameRenderer.getLightmapTextureManager();
        float tickDelta=1;
        double effectScale=client.options.getDarknessEffectScale().getValue();
        float darknessFade=client.player.getEffectFadeFactor(StatusEffects.DARKNESS,tickDelta)*(float)effectScale;
        float night=client.player.hasStatusEffect(StatusEffects.NIGHT_VISION)?GameRenderer.getNightVisionStrength(client.player,tickDelta):
                (client.player.hasStatusEffect(StatusEffects.CONDUIT_POWER)?client.player.getUnderwaterVisibility():0);
        float sky=attributes.getAttributeValue(EnvironmentAttributes.SKY_LIGHT_FACTOR_VISUAL,sourceCameraPosition);
        int ambientColor=0xffffff;
        var flash=world.getEndLightFlashManager();
        if(flash!=null) {
            ambientColor=0xfcffff; // End ambient has a slight cyan bias; bounded RGB protocol.
            if(!client.options.getHideLightningFlashes().getValue()) {
                float boost=flash.getSkyFactor(tickDelta);
                sky+=client.inGameHud.getBossBarHud().shouldThickenFog()?boost/3:boost;
            }
        }
        JsonObject out=new JsonObject();
        out.addProperty("skyFactor",finite(sky,0,4));
        out.addProperty("blockFactor",finite(lightmap.bridge$getFlickerIntensity()+1.5,0,4));
        out.addProperty("ambient",finite(world.getDimension().ambientLight(),0,1));
        out.addProperty("gamma",finite(client.options.getGamma().getValue()-darknessFade,0,1));
        out.addProperty("nightVision",finite(night,0,1));
        out.addProperty("darkness",finite(lightmap.bridge$getDarkness(client.player,darknessFade,tickDelta)*effectScale,0,1));
        out.addProperty("darkenWorld",finite(client.gameRenderer.getSkyDarkness(tickDelta),0,1));
        out.addProperty("skyColor",attributes.getAttributeValue(EnvironmentAttributes.SKY_LIGHT_COLOR_VISUAL,sourceCameraPosition)&0xffffff);
        out.addProperty("ambientColor",ambientColor);
        out.addProperty("hasSky",world.getDimension().hasSkyLight());
        packet.add("vanillaLight",out);
    }
    static double finite(double value,double minimum,double maximum) {return Double.isFinite(value)?Math.max(minimum,Math.min(maximum,value)):minimum;}
}

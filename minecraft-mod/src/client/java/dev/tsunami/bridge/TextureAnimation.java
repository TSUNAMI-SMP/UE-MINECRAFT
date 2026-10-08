package dev.tsunami.bridge;

import com.google.gson.*;
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.util.*;

/** Bake the active pack's frame order, durations and RGB interpolation into a
 * vertical strip. UE samples one slot per quantum; no animation JSON is lost. */
final class TextureAnimation {
    record Strip(BufferedImage pixels, int frames, int ticks, boolean shaderInterpolation) {}
    private record Frame(int index, int ticks) {}
    static Strip bake(BufferedImage source, JsonObject animation) throws IOException {
        boolean hasWidth=animation.has("width"),hasHeight=animation.has("height");
        int automatic=Math.min(source.getWidth(),source.getHeight());
        int w=hasWidth ? integer(animation.get("width")) : hasHeight ? source.getWidth() : automatic;
        int h=hasHeight ? integer(animation.get("height")) : hasWidth ? source.getHeight() : automatic;
        if(w<1 || h<1 || source.getWidth()%w!=0 || source.getHeight()%h!=0)
            throw new IOException("Invalid animation frame dimensions");
        int columns=source.getWidth()/w, available=columns*(source.getHeight()/h);
        if(available>4096) throw new IOException("Animation frame budget exceeded");
        int defaultTime=animation.has("frametime") ? integer(animation.get("frametime")) : 1;
        if(defaultTime<1 || defaultTime>32767) throw new IOException("Invalid animation duration");
        List<Frame> frames=new ArrayList<>();
        if(animation.has("frames")) {
            if(!animation.get("frames").isJsonArray()) throw new IOException("Invalid animation frames");
            for(JsonElement entry:animation.getAsJsonArray("frames")) {
                int index,time=defaultTime;
                if(entry.isJsonObject()) {
                    JsonObject object=entry.getAsJsonObject();index=integer(object.get("index"));
                    if(object.has("time")) time=integer(object.get("time"));
                } else index=integer(entry);
                if(index<0 || index>=available || time<1 || time>32767) throw new IOException("Invalid animation frame");
                frames.add(new Frame(index,time));
                if(frames.size()>4096) throw new IOException("Animation frame budget exceeded");
            }
        } else for(int i=0;i<available;i++) frames.add(new Frame(i,defaultTime));
        if(frames.isEmpty()) throw new IOException("Empty animation sequence");
        if(animation.has("interpolate") && (!animation.get("interpolate").isJsonPrimitive() || !animation.get("interpolate").getAsJsonPrimitive().isBoolean())) throw new IOException("Invalid interpolation flag");
        boolean interpolate=animation.has("interpolate") && animation.get("interpolate").getAsBoolean();
        int quantum=interpolate ? 1 : frames.getFirst().ticks;
        if(!interpolate) for(Frame frame:frames) quantum=gcd(quantum,frame.ticks);
        long slots=0;for(Frame frame:frames) slots+=frame.ticks/quantum;
        boolean shaderInterpolation=false;
        if(interpolate && (slots*h>16384 || slots*w*h>16_777_216) && frames.stream().allMatch(frame->frame.ticks==frames.getFirst().ticks)) {
            // Prismarine has 22 ordered frames lasting 300 ticks each. Keep its
            // compact source frames and perform display-RGB interpolation on
            // the GPU, rather than generating a 105600-pixel-tall texture.
            quantum=frames.getFirst().ticks;slots=frames.size();shaderInterpolation=true;
        }
        if(slots<1 || slots*h>16384 || slots*w*h>16_777_216 || w>2048)
            throw new IOException("Animated strip exceeds texture budget");
        BufferedImage strip=new BufferedImage(w,(int)slots*h,BufferedImage.TYPE_INT_ARGB);
        int output=0;
        for(int f=0;f<frames.size();f++) {
            Frame frame=frames.get(f),next=frames.get((f+1)%frames.size());
            for(int t=0;t<frame.ticks;t+=quantum) {
                double mix=interpolate && !shaderInterpolation ? (double)t/frame.ticks : 0;
                for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
                    int a=source.getRGB((frame.index%columns)*w+x,(frame.index/columns)*h+y);
                    int b=source.getRGB((next.index%columns)*w+x,(next.index/columns)*h+y);
                    // Sprite interpolation preserves the current frame's alpha.
                    int rgb=a&0xff000000;
                    for(int shift:new int[]{16,8,0}) rgb|=(int)(((a>>shift)&255)*(1-mix)+((b>>shift)&255)*mix)<<shift;
                    strip.setRGB(x,output*h+y,rgb);
                }
                ++output;
            }
        }
        return new Strip(strip,(int)slots,quantum,shaderInterpolation);
    }
    private static int integer(JsonElement value) throws IOException {
        if(value==null || !value.isJsonPrimitive() || !value.getAsJsonPrimitive().isNumber()) throw new IOException("Expected animation integer");
        double number=value.getAsDouble();
        if(!Double.isFinite(number) || number!=Math.rint(number) || Math.abs(number)>Integer.MAX_VALUE) throw new IOException("Invalid animation integer");
        return (int)number;
    }
    private static int gcd(int a,int b) {while(b!=0) {int r=a%b;a=b;b=r;}return a;}
}

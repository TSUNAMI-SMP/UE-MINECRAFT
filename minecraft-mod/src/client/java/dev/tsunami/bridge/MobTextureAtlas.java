package dev.tsunami.bridge;
/** Texture packing math shared by body and feature-layer export. */
final class MobTextureAtlas {
    private MobTextureAtlas() {}
    static double u(double value,int sourceWidth,int atlasWidth) {return value*sourceWidth/atlasWidth;}
    static double v(double value,int sourceHeight,int offset,int atlasHeight) {return (offset+value*sourceHeight)/atlasHeight;}
    static int tint(int pixel,int dye) {
        return (pixel&0xff000000) | ((((pixel>>16)&255)*((dye>>16)&255)/255)<<16)
            | ((((pixel>>8)&255)*((dye>>8)&255)/255)<<8) | ((pixel&255)*(dye&255)/255);
    }
}

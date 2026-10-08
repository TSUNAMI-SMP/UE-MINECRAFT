package dev.tsunami.bridge;
import org.joml.Matrix4f;
import org.joml.Vector3f;
/** 1.21.11 DiffuseLighting GUI lights and minecraft_mix_light transfer. */
final class NativeItemLighting {
    private static final Vector3f[] FLAT=lights(new Matrix4f().rotationY(-.3926991f).rotateX(2.3561945f));
    private static final Vector3f[] SIDE=lights(new Matrix4f().scaling(1,-1,1).rotateYXZ(1.0821041f,3.2375858f,0).rotateYXZ(-.3926991f,2.3561945f,0));
    private NativeItemLighting() {}
    private static Vector3f[] lights(Matrix4f matrix) {
        return new Vector3f[]{matrix.transformDirection(new Vector3f(.2f,1,-.7f).normalize()),matrix.transformDirection(new Vector3f(-.2f,1,.7f).normalize())};
    }
    // GuiRenderer.prepareItemInitially applies (size,-size,size) outside the
    // captured item pose. Its Y reflection also changes the shader normal.
    static float guiModel(float x,float y,float z,boolean sideLit) {return gui(x,-y,z,sideLit);}
    static float gui(float x,float y,float z,boolean sideLit) {
        float length=(float)Math.sqrt(x*x+y*y+z*z);if(length<1e-5f) return 1;
        x/=length;y/=length;z/=length;Vector3f[] lights=sideLit?SIDE:FLAT;
        float a=x*lights[0].x+y*lights[0].y+z*lights[0].z,b=x*lights[1].x+y*lights[1].y+z*lights[1].z;
        return Math.min(1,.4f+.6f*(Math.max(0,a)+Math.max(0,b)));
    }
}

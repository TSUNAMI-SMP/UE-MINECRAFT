package dev.tsunami.bridge;

/** Render-thread frame latch: sky camera and uploaded pixels always use the same frame. */
public final class VanillaSkyComposite {
    private static VideoClient source;
    private static VideoProtocol.Frame displayed;
    private static boolean active;
    private VanillaSkyComposite() {}
    public static void prepare(VideoClient next,boolean requested) {
        if(source!=next) {source=next;displayed=null;}
        if(next!=null) {var frame=next.poll();if(frame!=null) displayed=frame;}
        active=requested && next!=null && next.fresh() && displayed!=null && displayed.skyMask();
    }
    public static boolean active() {return active;}
    public static VideoProtocol.Frame displayed(VideoClient next) {return source==next ? displayed : null;}
    public static VideoProtocol.Camera camera() {return active ? displayed.camera() : null;}
    /** A letterboxed UE frame has the same focal length as this full-window sky projection. */
    public static float projectionFov(int windowWidth,int windowHeight) {
        if(!active || windowWidth<=0 || windowHeight<=0) return 80;
        double imageHeight=Math.min(windowHeight,(double)windowWidth*displayed.height()/displayed.width());
        return (float)Math.toDegrees(2*Math.atan(Math.tan(Math.toRadians(displayed.camera().verticalFov())/2)*windowHeight/imageHeight));
    }
}

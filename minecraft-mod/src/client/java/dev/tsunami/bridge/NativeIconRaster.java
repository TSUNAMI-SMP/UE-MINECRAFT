package dev.tsunami.bridge;

import com.google.gson.*;
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.util.*;

/** Bounded orthographic GUI icon bake from the native GUI model, UVs and tint.
 * Glint, animated frame changes and specialized shader effects remain explicitly unsupported.
 */
public final class NativeIconRaster {
    private NativeIconRaster() {}
    public static BufferedImage render(JsonArray faces,Map<String,BufferedImage> textures,int size) throws IOException {
        if(size<16 || size>128 || faces.size()>8192) throw new IOException("GUI icon budget exceeded");
        BufferedImage output=new BufferedImage(size,size,BufferedImage.TYPE_INT_ARGB);double[] depth=new double[size*size];Arrays.fill(depth,Double.NEGATIVE_INFINITY);
        ArrayList<JsonObject> sorted=new ArrayList<>();for(var face:faces) sorted.add(face.getAsJsonObject());
        sorted.sort(Comparator.comparingDouble(face->meanDepth(face.getAsJsonArray("vertices"))));
        for(JsonObject face:sorted) {
            BufferedImage texture=textures.get(face.get("texture").getAsString());if(texture==null) throw new IOException("GUI texture missing");
            JsonArray vertices=face.getAsJsonArray("vertices"),uv=face.getAsJsonArray("uv");
            if(vertices.size()!=4 || uv.size()!=4) throw new IOException("Invalid GUI quad");
            double[][] v=new double[4][5];for(int i=0;i<4;i++) {var p=vertices.get(i).getAsJsonArray();var t=uv.get(i).getAsJsonArray();
                v[i]=new double[]{(p.get(0).getAsDouble()+.5)*size,(.5-p.get(1).getAsDouble())*size,p.get(2).getAsDouble(),t.get(0).getAsDouble(),t.get(1).getAsDouble()};}
            int tint=face.get("color").getAsInt();triangle(output,depth,texture,tint,v[0],v[1],v[2]);triangle(output,depth,texture,tint,v[0],v[2],v[3]);
        }
        return output;
    }
    private static double meanDepth(JsonArray vertices) {double result=0;for(var v:vertices) result+=v.getAsJsonArray().get(2).getAsDouble();return result/4;}
    private static double edge(double[] a,double[] b,double x,double y) {return (x-a[0])*(b[1]-a[1])-(y-a[1])*(b[0]-a[0]);}
    private static boolean ownsEdge(double[] a,double[] b,double area) {
        double sign=Math.copySign(1,area),dy=(b[1]-a[1])*sign,dx=(b[0]-a[0])*sign;
        return dy>0 || (dy==0 && dx<0);
    }
    private static void triangle(BufferedImage output,double[] depths,BufferedImage texture,int tint,double[] a,double[] b,double[] c) {
        double area=edge(a,b,c[0],c[1]);if(Math.abs(area)<1e-9) return;
        int size=output.getWidth(),left=Math.max(0,(int)Math.floor(Math.min(a[0],Math.min(b[0],c[0])))),right=Math.min(size-1,(int)Math.ceil(Math.max(a[0],Math.max(b[0],c[0]))));
        int top=Math.max(0,(int)Math.floor(Math.min(a[1],Math.min(b[1],c[1])))),bottom=Math.min(size-1,(int)Math.ceil(Math.max(a[1],Math.max(b[1],c[1]))));
        for(int y=top;y<=bottom;y++) for(int x=left;x<=right;x++) {
            double u=edge(b,c,x+.5,y+.5)/area,v=edge(c,a,x+.5,y+.5)/area,w=1-u-v;
            if(u< -1e-9 || v< -1e-9 || w< -1e-9) continue;
            // Exactly one triangle owns a shared edge. Inclusive edges on both
            // triangles would blend a translucent quad twice along its diagonal.
            if((Math.abs(u)<=1e-9 && !ownsEdge(b,c,area)) || (Math.abs(v)<=1e-9 && !ownsEdge(c,a,area))
                    || (Math.abs(w)<=1e-9 && !ownsEdge(a,b,area))) continue;
            double z=a[2]*u+b[2]*v+c[2]*w;if(z<depths[y*size+x]-1e-8) continue;
            double tu=a[3]*u+b[3]*v+c[3]*w,tv=a[4]*u+b[4]*v+c[4]*w;
            int px=Math.max(0,Math.min(texture.getWidth()-1,(int)Math.floor(tu*texture.getWidth()))),py=Math.max(0,Math.min(texture.getHeight()-1,(int)Math.floor(tv*texture.getHeight())));
            int color=texture.getRGB(px,py),alpha=color>>>24;if(alpha==0) continue;
            int r=((color>>16)&255)*((tint>>16)&255)/255,g=((color>>8)&255)*((tint>>8)&255)/255,bl=(color&255)*(tint&255)/255;
            int previous=output.getRGB(x,y),oldAlpha=previous>>>24,newAlpha=alpha+oldAlpha*(255-alpha)/255;
            if(newAlpha>0) {r=(r*alpha+((previous>>16)&255)*oldAlpha*(255-alpha)/255)/newAlpha;g=(g*alpha+((previous>>8)&255)*oldAlpha*(255-alpha)/255)/newAlpha;bl=(bl*alpha+(previous&255)*oldAlpha*(255-alpha)/255)/newAlpha;}
            output.setRGB(x,y,(newAlpha<<24)|(r<<16)|(g<<8)|bl);depths[y*size+x]=z;
        }
    }
}

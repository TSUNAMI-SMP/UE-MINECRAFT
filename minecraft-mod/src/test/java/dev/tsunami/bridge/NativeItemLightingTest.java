package dev.tsunami.bridge;
import org.junit.Test;
import static org.junit.Assert.*;
public final class NativeItemLightingTest {
    @Test public void diffuseFacesHaveDirectionAndRemainBounded() {
        for(boolean side:new boolean[]{false,true}) {
            float min=1,max=0;
            for(float[] normal:new float[][]{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}) {
                float value=NativeItemLighting.gui(normal[0],normal[1],normal[2],side);
                assertTrue(value>=.4f && value<=1);min=Math.min(min,value);max=Math.max(max,value);
                assertEquals(value,NativeItemLighting.gui(normal[0]*7,normal[1]*7,normal[2]*7,side),1e-6);
            }
            assertTrue(max-min>.2f);
        }
        assertEquals(1,NativeItemLighting.gui(0,0,0,true),0);
    }
}

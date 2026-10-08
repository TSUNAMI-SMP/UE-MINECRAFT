package dev.tsunami.bridge;
import org.junit.Test;
import static org.junit.Assert.*;
public final class MobTextureAtlasTest {
    @Test public void layersOccupyDistinctUvRegionsEvenWithUnequalTextures() {
        assertEquals(.5,MobTextureAtlas.u(1,64,128),1e-10);
        assertEquals(1,MobTextureAtlas.u(1,128,128),1e-10);
        assertEquals(.3333333333,MobTextureAtlas.v(1,64,0,192),1e-9);
        assertEquals(.3333333333,MobTextureAtlas.v(0,128,64,192),1e-9);
        assertEquals(1,MobTextureAtlas.v(1,128,64,192),1e-10);
    }
    @Test public void dyedWoolPreservesOpacityAndTextureDetail() {
        assertEquals(0x80804020,MobTextureAtlas.tint(0x80808080,0xff8040));
        assertEquals(0x00ffffff,MobTextureAtlas.tint(0x00ffffff,0xffffff));
        assertEquals(0xff123456,MobTextureAtlas.tint(0xff123456,0xffffff));
    }
}

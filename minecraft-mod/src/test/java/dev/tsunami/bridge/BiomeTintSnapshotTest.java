package dev.tsunami.bridge;

import org.junit.Test;
import static org.junit.Assert.*;

public class BiomeTintSnapshotTest {
    @Test public void paletteDeduplicatesAllSourcePositionsIncludingAir() {
        int[][] colors=new int[512][3];
        for(int i=0;i<512;++i) colors[i]=new int[]{0x91bd59,0x77ab2f,0xa68f65};
        colors[7+6*8+5*64]=new int[]{0x80b497,0x60a17b,0xad9774};
        var data=BiomeTintSnapshot.encode(colors);
        assertEquals(2,data.getAsJsonArray("palette").size());
        assertEquals(512,data.getAsJsonArray("indices").size());
        assertEquals(1,data.getAsJsonArray("indices").get(375).getAsInt());
        assertEquals(0,data.getAsJsonArray("indices").get(374).getAsInt());
        assertEquals(0x80b497,data.getAsJsonArray("palette").get(1).getAsJsonArray().get(0).getAsInt());
    }
    @Test public void invalidOrIncompleteColorFieldsCannotBeExported() {
        int[][] valid=new int[512][3];
        for(int[][] invalid:new int[][][]{null,new int[511][3]}) {
            try {BiomeTintSnapshot.encode(invalid);fail("Invalid size accepted");} catch(IllegalArgumentException expected) {}
        }
        for(int[] invalid:new int[][]{null,new int[]{0,0},new int[]{-1,0,0},new int[]{0xffffff+1,0,0}}) {
            valid[100]=invalid;
            try {BiomeTintSnapshot.encode(valid);fail("Invalid color accepted");} catch(IllegalArgumentException expected) {}
        }
    }
}

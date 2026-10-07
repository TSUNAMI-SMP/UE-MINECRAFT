package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import org.junit.*;
import org.junit.rules.TemporaryFolder;
import static org.junit.Assert.*;

public class NativeExportDataTest {
    @Rule public TemporaryFolder temp=new TemporaryFolder();

    private static WorldSnapshot.Shape voxel(int x,int y,int z,String id,String state,int color,int sky,int block,int opacity,int emission) {
        // World ownership is absolute; geometry is relative to the exported spawn.
        return new WorldSnapshot.Shape(.5,.5,.5,color,1,1,1,id,false,x,y,z,state,1,sky,block,opacity,emission);
    }
    private static WorldSnapshot.Shape stone(int x,int y,int z) {
        return voxel(x,y,z,"minecraft:stone","",0xffffff,15,0,15,0);
    }
    private static void rejects(Runnable action) {
        try {action.run();fail("Invalid cell was accepted");} catch(IllegalArgumentException expected) {}
    }

    @Test public void everyVoxelInNegativeCellPreservesOwnershipAndLight() {
        var cell=new WorldSnapshot.Cell(-2,-3,4);List<WorldSnapshot.Shape> source=new ArrayList<>();int[] skyTop=new int[64];
        for(int z=0;z<8;z++) for(int x=0;x<8;x++) skyTop[z*8+x]=(z+x)%16;
        for(int y=0;y<8;y++) for(int z=0;z<8;z++) for(int x=0;x<8;x++)
            source.add(voxel(cell.x()*8+x,cell.y()*8+y,cell.z()*8+z,"minecraft:stone","",0x91bd59,(x+y)%16,(z+y)%16,15,0));
        JsonObject encoded=NativeExportData.cell(cell,source,skyTop);
        assertEquals("cell",encoded.get("type").getAsString());assertEquals(512,encoded.getAsJsonArray("blocks").size());
        assertEquals(1,encoded.getAsJsonArray("palette").size());Set<String> owners=new HashSet<>();
        for(var value:encoded.getAsJsonArray("blocks")) {
            var row=value.getAsJsonArray();int local=row.get(0).getAsInt();
            int x=cell.x()*8+(local&7),y=cell.y()*8+(local/64),z=cell.z()*8+((local/8)%8);
            assertTrue(owners.add(x+","+y+","+z));assertEquals(0,row.get(1).getAsInt());
            assertEquals(((local&7)+(local/64))%16,row.get(2).getAsInt());
            assertEquals((((local/8)%8)+(local/64))%16,row.get(3).getAsInt());
        }
        assertEquals(512,owners.size());for(int i=0;i<64;i++) assertEquals(skyTop[i],encoded.getAsJsonArray("skyTop").get(i).getAsInt());
    }

    @Test public void dictionarySeparatesStateTintOpacityAndEmissionButNotPerVoxelLight() {
        var source=List.of(
            voxel(0,0,0,"minecraft:oak_leaves","persistent=false",0x48b518,15,0,1,0),
            voxel(1,0,0,"minecraft:oak_leaves","persistent=false",0x48b518,7,2,1,0),
            voxel(2,0,0,"minecraft:oak_leaves","persistent=true",0x48b518,15,0,1,0),
            voxel(3,0,0,"minecraft:oak_leaves","persistent=false",0x61ab43,15,0,1,0),
            voxel(4,0,0,"minecraft:oak_leaves","persistent=false",0x48b518,15,0,15,0),
            voxel(5,0,0,"minecraft:oak_leaves","persistent=false",0x48b518,15,0,1,3));
        var encoded=NativeExportData.cell(new WorldSnapshot.Cell(0,0,0),source,new int[64]);
        assertEquals(5,encoded.getAsJsonArray("palette").size());
        assertEquals(0,encoded.getAsJsonArray("blocks").get(0).getAsJsonArray().get(1).getAsInt());
        assertEquals(0,encoded.getAsJsonArray("blocks").get(1).getAsJsonArray().get(1).getAsInt());
        var first=encoded.getAsJsonArray("palette").get(0).getAsJsonArray();
        assertEquals("minecraft:oak_leaves",first.get(0).getAsString());assertEquals("persistent=false",first.get(1).getAsString());
        assertEquals(0x48b518,first.get(2).getAsInt());assertEquals(1,first.get(3).getAsInt());assertEquals(0,first.get(4).getAsInt());
    }

    @Test public void emptyCellsKeepSkySeedsAndExplicitEmptyGeometry() {
        int[] sky=new int[64];Arrays.fill(sky,15);
        var encoded=NativeExportData.cell(new WorldSnapshot.Cell(0,0,0),List.of(),sky);
        assertTrue(encoded.getAsJsonArray("blocks").isEmpty());assertTrue(encoded.getAsJsonArray("palette").isEmpty());
        assertEquals(64,encoded.getAsJsonArray("skyTop").size());assertEquals(15,encoded.getAsJsonArray("skyTop").get(63).getAsInt());
    }

    @Test public void rejectsDuplicateOwnershipWrongCellsAndNonLogicalRows() {
        var cell=new WorldSnapshot.Cell(0,0,0);var one=stone(0,0,0);
        rejects(()->NativeExportData.cell(cell,List.of(one,one),new int[64]));
        rejects(()->NativeExportData.cell(cell,List.of(stone(-1,0,0)),new int[64]));
        rejects(()->NativeExportData.cell(cell,List.of(stone(8,0,0)),new int[64]));
        var collision=new WorldSnapshot.Shape(.5,.5,.5,0,1,1,1,"minecraft:stone",true,0,0,0,"",2);
        rejects(()->NativeExportData.cell(cell,List.of(collision),new int[64]));
        rejects(()->NativeExportData.cell(cell,List.of(new WorldSnapshot.Shape(.5,.5,.5,0,1,1,1,"",false,0,0,0,"",1)),new int[64]));
    }

    @Test public void rejectsOverlargeRowsInvalidSkyAndWrappedCellCoordinates() {
        var cell=new WorldSnapshot.Cell(0,0,0);
        rejects(()->NativeExportData.cell(cell,Collections.nCopies(513,stone(0,0,0)),new int[64]));
        rejects(()->NativeExportData.cell(cell,List.of(),new int[63]));
        for(int bad:new int[]{-1,16}) {int[] sky=new int[64];sky[63]=bad;rejects(()->NativeExportData.cell(cell,List.of(),sky));}
        // Multiplication in int would wrap this coordinate to zero and admit the unrelated owner.
        rejects(()->NativeExportData.cell(new WorldSnapshot.Cell(536_870_912,0,0),List.of(stone(0,0,0)),new int[64]));
    }

    @Test public void finiteCoordinatesOnly() {
        for(double bad:new double[]{Double.NaN,Double.POSITIVE_INFINITY,Double.NEGATIVE_INFINITY}) rejects(()->NativeExportData.array(1,bad,3));
        assertEquals(-0.25,NativeExportData.array(-.25,2.5).get(0).getAsDouble(),0);
    }

    @Test public void referenceBindsExactRelativeFileContentAndSize() throws Exception {
        Path root=temp.newFolder("package").toPath(),file=root.resolve("ui/items/日本語 icon.bin");Files.createDirectories(file.getParent());
        Files.writeString(file,"abc",StandardCharsets.UTF_8);var reference=NativeExportData.reference(root,file);
        assertEquals("ui/items/日本語 icon.bin",reference.get("file").getAsString());assertEquals(3,reference.get("bytes").getAsLong());
        assertEquals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",reference.get("sha256").getAsString());
        Files.writeString(file,"abcd");assertNotEquals(reference.get("sha256").getAsString(),NativeExportData.sha256(file));
    }

    @Test public void referencesRejectExternalAndMissingFiles() throws Exception {
        Path root=temp.newFolder("package").toPath(),outside=temp.newFile("outside.bin").toPath();
        for(Path file:List.of(outside,root.resolve("missing.bin"),root)) {
            try {NativeExportData.reference(root,file);fail(file.toString());} catch(IOException expected) {}
        }
    }

    @Test public void referencesRejectSymlinksIncludingAncestorDirectories() throws Exception {
        Path root=temp.newFolder("package").toPath(),outside=temp.newFolder("external").toPath();Files.writeString(outside.resolve("asset.bin"),"outside");
        try {Files.createSymbolicLink(root.resolve("link"),outside);Files.createSymbolicLink(root.resolve("direct.bin"),outside.resolve("asset.bin"));}
        catch(UnsupportedOperationException|IOException|SecurityException unavailable) {Assume.assumeNoException(unavailable);}
        for(Path file:List.of(root.resolve("direct.bin"),root.resolve("link/asset.bin"))) {
            try {NativeExportData.reference(root,file);fail("Symlink reference was admitted: "+file);} catch(IOException expected) {}
        }
    }

    @Test public void publishedManifestIsUtf8AndNeverOverwritesPreviousPackage() throws Exception {
        Path root=temp.newFolder("package").toPath();JsonObject first=new JsonObject();first.addProperty("name","テスト");
        Path manifest=NativeExportData.writeManifest(root,first);byte[] previous=Files.readAllBytes(manifest);
        assertEquals("テスト",JsonParser.parseString(new String(previous,StandardCharsets.UTF_8)).getAsJsonObject().get("name").getAsString());
        JsonObject next=new JsonObject();next.addProperty("name","replacement");
        try {NativeExportData.writeManifest(root,next);fail("Existing package was overwritten");} catch(FileAlreadyExistsException expected) {}
        assertArrayEquals(previous,Files.readAllBytes(manifest));
    }
}

package dev.tsunami.bridge;

import java.util.List;
import java.util.Map;
import net.minecraft.block.enums.*;
import net.minecraft.state.property.BooleanProperty;
import net.minecraft.state.property.EnumProperty;
import net.minecraft.state.property.IntProperty;
import net.minecraft.state.property.Property;
import net.minecraft.util.math.Direction;
import org.junit.Test;
import static org.junit.Assert.*;

public class BlockGeometryCaptureTest {
    @Test public void placementFacesUseNativeSerializedValuesInsteadOfDebugEnumNames() {
        var face=EnumProperty.of("face",BlockFace.class);
        assertEquals("WALL",BlockFace.WALL.toString());
        assertEquals("wall",BlockGeometryCapture.valueName(face,BlockFace.WALL));
        assertEquals("face=wall,facing=north",TextureExport.stateKey(Map.of(
            "face",BlockGeometryCapture.valueName(face,BlockFace.WALL),
            "facing",BlockGeometryCapture.valueName(EnumProperty.of("facing",Direction.class),Direction.NORTH))));
    }
    @Test public void nativePlacementPropertyFamiliesRoundTripWithoutRegistryBootstrap() {
        // Real 1.21.11 serializers. A plain JUnit JVM does not apply Fabric's
        // package-access transforms required for registry bootstrap.
        List<Property<?>> properties=List.of(
            EnumProperty.of("face",BlockFace.class),EnumProperty.of("facing",Direction.class),
            EnumProperty.of("axis",Direction.Axis.class),EnumProperty.of("half",BlockHalf.class),
            EnumProperty.of("half",DoubleBlockHalf.class),EnumProperty.of("type",SlabType.class),
            EnumProperty.of("shape",StairShape.class),EnumProperty.of("hinge",DoorHinge.class),
            EnumProperty.of("east",WallShape.class),EnumProperty.of("leaves",BambooLeaves.class),
            EnumProperty.of("attachment",Attachment.class),EnumProperty.of("side_chain",SideChainPart.class),
            BooleanProperty.of("waterlogged"),IntProperty.of("power",0,15));
        int values=0,differences=0;
        for(var property:properties) for(var value:property.getValues()) {
            String actual=BlockGeometryCapture.valueName(property,value);values++;
            assertTrue(property.getName()+"="+actual,actual.matches("[a-z0-9_]+"));
            assertEquals(property.getName()+" round trip",value,property.parse(actual).orElseThrow());
            if(!actual.equals(value.toString())) differences++;
        }
        assertTrue(values>50);
        assertTrue("The regression includes debug enum names",differences>0);
        System.out.println("Native property serialization: "+properties.size()+" families / "+values+" values / "+differences+" debug-name differences");
    }
}

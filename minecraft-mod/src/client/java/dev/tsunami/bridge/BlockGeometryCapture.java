package dev.tsunami.bridge;

import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.block.BlockRenderType;
import net.minecraft.client.MinecraftClient;
import net.minecraft.registry.Registries;
import net.minecraft.state.property.Property;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import java.util.*;

/** Read-only native state/voxel snapshots taken on the client thread, never a server edit. */
public final class BlockGeometryCapture {
    private BlockGeometryCapture() {}
    private static final Set<String> SPECIAL=Set.of("minecraft:nether_portal","minecraft:end_portal","minecraft:end_gateway",
        "minecraft:fire","minecraft:soul_fire","minecraft:piston","minecraft:sticky_piston","minecraft:piston_head",
        "minecraft:moving_piston","minecraft:redstone_wire","minecraft:tripwire","minecraft:tripwire_hook",
        "minecraft:structure_void","minecraft:light","minecraft:barrier","minecraft:bubble_column");
    public static String exclusion(BlockState state) {
        String id=Registries.BLOCK.getId(state.getBlock()).toString();
        if(!id.startsWith("minecraft:")) return "modded block outside vanilla scope";
        if(state.isAir()) return "air";
        if(state.isLiquid() || id.equals("minecraft:bubble_column")) return "fluid";
        if(id.endsWith("_sign") || id.endsWith("_hanging_sign")) return "sign";
        if(state.hasBlockEntity()) return "block entity / dedicated renderer or inventory logic";
        if(state.getRenderType()!=BlockRenderType.MODEL) return "non-model renderer";
        if(SPECIAL.contains(id)) return "special block mechanics";
        return "";
    }
    public static boolean supported(BlockState state) { return exclusion(state).isEmpty(); }
    public static Map<String,String> properties(BlockState state) {
        Map<String,String> result=new TreeMap<>();
        state.getEntries().forEach((key,value)->result.put(key.getName(),valueName(key,value))); return result;
    }
    // Enum.toString() can be a Java enum/debug name, not the serialized value used
    // by vanilla blockstate JSON. Use the property's own serializer for both exports and world rows.
    static <T extends Comparable<T>> String valueName(Property<T> property,Comparable<?> value) {
        return property.name(property.getType().cast(value));
    }
    public static String stateKey(BlockState state) { return TextureExport.stateKey(properties(state)); }
    private static List<double[]> boxes(List<Box> boxes,net.minecraft.util.math.Vec3d offset) {
        List<double[]> result=new ArrayList<>();
        for(Box b:boxes) if(b.maxX>b.minX && b.maxY>b.minY && b.maxZ>b.minZ)
            result.add(new double[]{b.minX-offset.x,b.minY-offset.y,b.minZ-offset.z,b.maxX-offset.x,b.maxY-offset.y,b.maxZ-offset.z});
        return List.copyOf(result);
    }
    public static TextureExport.Block capture(Block block,MinecraftClient mc) {
        BlockState defaults=block.getDefaultState(); String reason=exclusion(defaults);
        String id=Registries.BLOCK.getId(block).toString(); var props=properties(defaults);
        if(!reason.isEmpty()) return new TextureExport.Block(id,props,0xffffff,List.of(),reason);
        BlockPos pos=mc.player==null ? BlockPos.ORIGIN : mc.player.getBlockPos();
        if(mc.world==null) return new TextureExport.Block(id,props,0xffffff,List.of(),"open a local world before exporting state shapes");
        int color=mc.getBlockColors().getColor(defaults,mc.world,pos,0)&0xffffff;
        // BlockColors is a render provider, not the particle provider. Grass top
        // and side overlay need this biome color even though grass dust is white.
        Map<Integer,Integer> renderTints=new TreeMap<>();
        for(int tintIndex=0;tintIndex<16;tintIndex++)
            renderTints.put(tintIndex,mc.getBlockColors().getColor(defaults,mc.world,pos,tintIndex)&0xffffff);
        List<TextureExport.State> states=new ArrayList<>();
        for(BlockState state:block.getStateManager().getStates()) {
            var offset=state.getModelOffset(pos);
            List<String> solidFaces=new ArrayList<>();
            for(var direction:net.minecraft.util.math.Direction.values()) if(state.isSideSolidFullSquare(mc.world,pos,direction)) solidFaces.add(direction.asString());
            // Vanilla offset blocks with solid hulls (bamboo/pointed dripstone)
            // apply the same offset to collision as to their outline/rendered model.
            states.add(new TextureExport.State(properties(state),boxes(state.getCollisionShape(mc.world,pos).getBoundingBoxes(),offset),
                boxes(state.getOutlineShape(mc.world,pos).getBoundingBoxes(),offset),List.copyOf(solidFaces),Block.cannotConnect(state),state.isOpaqueFullCube(),state.getLuminance(),state.getOpacity()));
        }
        var zeroOffset=defaults.getModelOffset(BlockPos.ORIGIN);
        return new TextureExport.Block(id,props,color,List.copyOf(states),"",Math.abs(zeroOffset.x),Math.abs(zeroOffset.y),Map.copyOf(renderTints));
    }
}

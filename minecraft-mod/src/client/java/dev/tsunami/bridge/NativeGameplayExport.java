package dev.tsunami.bridge;

import com.google.gson.*;
import com.mojang.serialization.JsonOps;
import java.io.IOException;
import java.util.*;
import java.util.concurrent.TimeUnit;
import net.minecraft.client.MinecraftClient;
import net.minecraft.item.*;
import net.minecraft.recipe.*;
import net.minecraft.recipe.input.*;
import net.minecraft.registry.*;

/** Snapshot the loaded datapack on its owning server thread; UE needs no server at runtime. */
public final class NativeGameplayExport {
    private NativeGameplayExport() {}
    public static JsonObject capture(MinecraftClient client,int[] bounds) throws IOException {
        var server=client.getServer();
        if(server==null) throw new IOException("Standalone gameplay export requires an integrated Minecraft world");
        var dimension=client.world.getRegistryKey();
        try {return server.submit(()-> {
            JsonObject data=new JsonObject(),fuels=new JsonObject(),remainders=new JsonObject();
            JsonArray recipes=new JsonArray(),unsupported=new JsonArray();
            var lookup=server.getRegistryManager();var ops=RegistryOps.of(JsonOps.INSTANCE,lookup);
            var entries=new ArrayList<>(server.getRecipeManager().values());
            entries.sort(Comparator.comparing(entry->entry.id().getValue().toString()));
            if(entries.size()>32768) throw new IllegalStateException("Gameplay recipe budget exceeded");
            for(var entry:entries) {
                Recipe<?> recipe=entry.value();JsonObject row=new JsonObject();
                row.addProperty("id",entry.id().getValue().toString());
                row.addProperty("type",Registries.RECIPE_SERIALIZER.getId(recipe.getSerializer()).toString());
                row.add("source",Recipe.CODEC.encodeStart(ops,recipe).getOrThrow());
                JsonArray inputs=new JsonArray();ItemStack result=ItemStack.EMPTY;
                if(recipe instanceof ShapedRecipe shaped) {
                    row.addProperty("width",shaped.getWidth());row.addProperty("height",shaped.getHeight());
                    for(var ingredient:shaped.getIngredients()) inputs.add(ingredient.map(NativeGameplayExport::ingredient).orElseGet(JsonArray::new));
                    result=shaped.craft(CraftingRecipeInput.create(3,3,Collections.nCopies(9,ItemStack.EMPTY)),lookup);
                } else if(recipe instanceof ShapelessRecipe shapeless) {
                    for(var ingredient:shapeless.getIngredientPlacement().getIngredients()) inputs.add(ingredient(ingredient));
                    result=shapeless.craft(CraftingRecipeInput.create(3,3,Collections.nCopies(9,ItemStack.EMPTY)),lookup);
                } else if(recipe instanceof SingleStackRecipe single) {
                    inputs.add(ingredient(single.ingredient()));result=single.craft(new SingleStackRecipeInput(ItemStack.EMPTY),lookup);
                    if(single instanceof AbstractCookingRecipe cooking) {row.addProperty("ticks",cooking.getCookingTime());row.addProperty("experience",cooking.getExperience());}
                }
                row.add("ingredients",inputs);
                if(!result.isEmpty()) {row.addProperty("result",Registries.ITEM.getId(result.getItem()).toString());row.addProperty("count",result.getCount());row.add("resultStack",ItemStack.CODEC.encodeStart(ops,result).getOrThrow());}
                else unsupported.add(row.get("id"));
                recipes.add(row);
            }
            var fuel=FuelRegistry.createDefault(lookup,server.getOverworld().getEnabledFeatures());
            for(Item item:Registries.ITEM) {
                var stack=item.getDefaultStack();String id=Registries.ITEM.getId(item).toString();
                int ticks=fuel.getFuelTicks(stack);if(ticks>0) fuels.addProperty(id,ticks);
                ItemStack remainder=item.getRecipeRemainder();if(!remainder.isEmpty()) remainders.addProperty(id,Registries.ITEM.getId(remainder.getItem()).toString());
            }
            data.addProperty("version",1);data.add("recipes",recipes);data.add("fuels",fuels);data.add("remainders",remainders);data.add("dynamicRecipes",unsupported);
            var world=server.getWorld(dimension);if(world==null) throw new IllegalStateException("Source dimension is unavailable");
            data.addProperty("tickRate",20);data.addProperty("randomTickSpeed",world.getGameRules().getValue(net.minecraft.world.rule.GameRules.RANDOM_TICK_SPEED));
            JsonArray containers=new JsonArray();
            if(bounds!=null) for(int cx=Math.floorDiv(bounds[0],16);cx<=Math.floorDiv(bounds[3],16);cx++) for(int cz=Math.floorDiv(bounds[2],16);cz<=Math.floorDiv(bounds[5],16);cz++) {
                var chunk=world.getChunk(cx,cz,net.minecraft.world.chunk.ChunkStatus.FULL,false);
                if(!(chunk instanceof net.minecraft.world.chunk.WorldChunk loaded)) continue;
                for(var entity:loaded.getBlockEntities().values()) {
                    var pos=entity.getPos();if(pos.getX()<bounds[0] || pos.getX()>bounds[3] || pos.getY()<bounds[1] || pos.getY()>bounds[4] || pos.getZ()<bounds[2] || pos.getZ()>bounds[5]) continue;
                    if(!(entity instanceof net.minecraft.inventory.Inventory inventory)) continue;
                    String kind=Registries.BLOCK.getId(entity.getCachedState().getBlock()).getPath();if(kind.equals("barrel") || kind.equals("trapped_chest")) kind="chest";
                    if(!Set.of("chest","furnace","blast_furnace","smoker","hopper","dropper","dispenser").contains(kind)) continue;
                    if(containers.size()>=4096) throw new IllegalStateException("Container export budget exceeded");
                    JsonObject container=new JsonObject();container.addProperty("key",pos.getX()+","+pos.getY()+","+pos.getZ());container.addProperty("kind",kind);
                    JsonArray slots=new JsonArray();for(int slot=0;slot<inventory.size();slot++) {var stack=inventory.getStack(slot);JsonObject item=new JsonObject();item.addProperty("item",stack.isEmpty() ? "" : Registries.ITEM.getId(stack.getItem()).toString());item.addProperty("count",stack.getCount());if(!stack.isEmpty()) item.add("sourceStack",ItemStack.CODEC.encodeStart(ops,stack).getOrThrow());slots.add(item);}container.add("slots",slots);
                    var nbt=entity.createNbt(lookup);container.addProperty("burn",nbt.getInt("lit_time_remaining",0));container.addProperty("burnTotal",nbt.getInt("lit_total_time",0));container.addProperty("cook",nbt.getInt("cooking_time_spent",0));containers.add(container);
                }
            }
            data.add("containers",containers);
            return data;
        }).get(30,TimeUnit.SECONDS);} catch(Exception error) {throw new IOException("Cannot snapshot integrated-server gameplay data",error);}
    }
    private static JsonArray ingredient(Ingredient ingredient) {
        JsonArray result=new JsonArray();ingredient.getMatchingItems().map(entry->Registries.ITEM.getId(entry.value()).toString()).sorted().forEach(result::add);return result;
    }
}

package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import java.util.UUID;
import org.junit.Test;
import static org.junit.Assert.*;

public final class MobBridgeModelsTest {
    private static final String ZOMBIE="a".repeat(64),VILLAGER="b".repeat(64);
    private JsonObject manifest() {
        var manifest=new JsonObject();manifest.addProperty("kind","mobs");manifest.addProperty("version",1);
        var appearances=new JsonObject();var zombie=new JsonObject();zombie.addProperty("type","minecraft:zombie");
        var villager=new JsonObject();villager.addProperty("type","minecraft:villager");
        appearances.add(ZOMBIE,zombie);appearances.add(VILLAGER,villager);manifest.add("appearances",appearances);
        var entities=new JsonObject();entities.addProperty(UUID.randomUUID().toString(),ZOMBIE);manifest.add("entities",entities);return manifest;
    }
    @Test public void nearbyOnlyExportStillResolvesNewZombieAndVillagerUuidsByType() {
        var models=MobBridge.parseModels(manifest());assertEquals(ZOMBIE,models.templates().get("minecraft:zombie"));
        assertEquals(VILLAGER,models.templates().get("minecraft:villager"));
    }
    @Test public void explicitTemplatePreservesCorrectSpecies() {
        var manifest=manifest();var templates=new JsonObject();templates.addProperty("minecraft:zombie",ZOMBIE);manifest.add("templates",templates);
        assertEquals(ZOMBIE,MobBridge.parseModels(manifest).templates().get("minecraft:zombie"));
        templates.addProperty("minecraft:villager",ZOMBIE);
        try {MobBridge.parseModels(manifest);fail("Wrong species template accepted");}catch(IllegalArgumentException expected) { }
    }
    @Test public void babyOnlyAppearanceIsNotUsedAsAdultDefault() {
        var manifest=manifest();var stats=new JsonObject();stats.addProperty("baby",true);manifest.getAsJsonObject("appearances").getAsJsonObject(ZOMBIE).add("stats",stats);
        assertFalse(MobBridge.parseModels(manifest).templates().containsKey("minecraft:zombie"));
    }
    @Test public void scopeUsesInclusiveEightBlockCellsAndResistsIntegerOverflow() {
        assertTrue(MobBridge.inScope(12,2,-12,0,0,0,12,2));assertFalse(MobBridge.inScope(13,0,0,0,0,0,12,2));
        assertFalse(MobBridge.inScope(Integer.MIN_VALUE,0,0,Integer.MAX_VALUE,0,0,12,2));
    }
}

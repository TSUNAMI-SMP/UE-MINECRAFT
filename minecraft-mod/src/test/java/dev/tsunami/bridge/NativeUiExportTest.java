package dev.tsunami.bridge;

import java.awt.image.BufferedImage;
import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.function.Predicate;
import java.util.stream.Stream;
import javax.imageio.ImageIO;
import net.minecraft.resource.*;
import net.minecraft.util.Identifier;
import org.junit.Test;
import static org.junit.Assert.*;

public class NativeUiExportTest {
    private static final List<String> PHASES=List.of("full_moon","waning_gibbous","third_quarter","waning_crescent",
            "new_moon","waxing_crescent","first_quarter","waxing_gibbous");
    private static class SelectedResources implements ResourceManager {
        final Map<Identifier,Resource> entries=new HashMap<>();
        public Optional<Resource> getResource(Identifier id) {return Optional.ofNullable(entries.get(id));}
        public Set<String> getAllNamespaces() {return Set.of("minecraft");}
        public List<Resource> getAllResources(Identifier id) {return getResource(id).stream().toList();}
        public Map<Identifier,Resource> findResources(String start,Predicate<Identifier> filter) {
            Map<Identifier,Resource> result=new HashMap<>();
            entries.forEach((id,resource)->{if(id.getPath().startsWith(start+"/") && filter.test(id)) result.put(id,resource);});
            return result;
        }
        public Map<Identifier,List<Resource>> findAllResources(String start,Predicate<Identifier> filter) {
            Map<Identifier,List<Resource>> result=new HashMap<>();findResources(start,filter).forEach((id,resource)->result.put(id,List.of(resource)));return result;
        }
        public Stream<ResourcePack> streamResourcePacks() {return Stream.empty();}
        Resource put(String id,byte[] pixels) {Resource value=new Resource(null,()->new ByteArrayInputStream(pixels));entries.put(Identifier.of(id),value);return value;}
    }
    private static byte[] png(int width,int height,int color) throws IOException {
        BufferedImage image=new BufferedImage(width,height,BufferedImage.TYPE_INT_ARGB);
        for(int y=0;y<height;y++) for(int x=0;x<width;x++) image.setRGB(x,y,color);
        ByteArrayOutputStream output=new ByteArrayOutputStream();assertTrue(ImageIO.write(image,"png",output));return output.toByteArray();
    }
    @Test public void currentCelestialPathsExistInActualMinecraft12111Client() throws Exception {
        List<String> celestial=NativeUiExport.additionalSprites().stream().filter(id->id.contains("/celestial/")).toList();
        assertEquals(9,celestial.size());
        for(String id:celestial) try(var resource=getClass().getResourceAsStream("/assets/minecraft/"+Identifier.of(id).getPath())) {
            assertNotNull("Actual vanilla texture missing: "+id,resource);assertNotNull(ImageIO.read(resource));
        }
        for(String phase:PHASES) assertTrue(celestial.contains("minecraft:textures/environment/celestial/moon/"+phase+".png"));
    }
    @Test public void creativePanelsAndAttackSpritesExistInVanilla12111() throws Exception {
        for(String file:List.of("textures/gui/container/creative_inventory/tab_inventory.png", "textures/gui/container/creative_inventory/tab_items.png", "textures/gui/container/creative_inventory/tab_item_search.png",
                "textures/gui/sprites/hud/crosshair_attack_indicator_background.png", "textures/gui/sprites/hud/crosshair_attack_indicator_progress.png", "textures/gui/sprites/hud/crosshair_attack_indicator_full.png",
                "textures/gui/sprites/container/creative_inventory/scroller.png", "textures/gui/sprites/container/creative_inventory/tab_top_selected_1.png", "textures/gui/sprites/container/creative_inventory/tab_bottom_unselected_7.png")) {
            try(var resource=getClass().getResourceAsStream("/assets/minecraft/"+file)) {assertNotNull(file,resource);assertNotNull(ImageIO.read(resource));}
        }
    }
    @Test public void activePoofSpriteOrderAndOverridesArePreserved() throws Exception {
        SelectedResources manager=new SelectedResources();byte[] pixels=png(4,4,0xff123456);
        manager.put("minecraft:particles/poof.json","{\"textures\":[\"pack:replacement\",\"minecraft:generic_0\",\"pack:replacement\"]}".getBytes(java.nio.charset.StandardCharsets.UTF_8));
        Resource selected=manager.put("pack:textures/particle/replacement.png",pixels);manager.put("minecraft:textures/particle/generic_0.png",pixels);
        Map<Identifier,Resource> resources=new HashMap<>();var frames=NativeUiExport.collectDeathPoof(manager,resources);
        assertEquals(List.of(Identifier.of("pack:textures/particle/replacement.png"),Identifier.of("minecraft:textures/particle/generic_0.png"),Identifier.of("pack:textures/particle/replacement.png")),frames);
        assertSame(selected,resources.get(frames.getFirst()));assertEquals(2,resources.size());
        manager.entries.remove(Identifier.of("minecraft:textures/particle/generic_0.png"));
        assertThrows(IOException.class,()->NativeUiExport.collectDeathPoof(manager,new HashMap<>()));
    }
    @Test public void vanillaPoofDefinitionUsesActualLocalSpriteAssets() throws Exception {
        SelectedResources manager=new SelectedResources();
        try(var input=getClass().getResourceAsStream("/assets/minecraft/particles/poof.json")) {assertNotNull(input);manager.put("minecraft:particles/poof.json",input.readAllBytes());}
        var json=com.google.gson.JsonParser.parseString(new String(manager.getResource(Identifier.of("minecraft:particles/poof.json")).get().getInputStream().readAllBytes(),java.nio.charset.StandardCharsets.UTF_8));
        for(var value:json.getAsJsonObject().getAsJsonArray("textures")) {
            var id=Identifier.of(value.getAsString());var file=Identifier.of(id.getNamespace(),"textures/particle/"+id.getPath()+".png");
            try(var input=getClass().getResourceAsStream("/assets/"+file.getNamespace()+"/"+file.getPath())) {assertNotNull(file.toString(),input);manager.put(file.toString(),input.readAllBytes());}
        }
        var frames=NativeUiExport.collectDeathPoof(manager,new HashMap<>());assertEquals(8,frames.size());
    }
    @Test public void moonPhaseIndexMatchesActualVanillaCelestialTextureName() {
        for(var phase:net.minecraft.world.MoonPhase.values()) assertEquals(phase.asString(),PHASES.get(phase.getIndex()));
        for(var skybox:net.minecraft.world.dimension.DimensionType.Skybox.values())
            assertTrue(Set.of("overworld","end","none").contains(skybox.asString()));
    }
    @Test public void activePackPixelsAndChecksumsSurviveCelestialCollectionAndCopy() throws Exception {
        SelectedResources manager=new SelectedResources();byte[] packSun=png(7,11,0xff443322);
        Resource selected=manager.put("minecraft:textures/environment/celestial/sun.png",packSun);
        var directory=Files.createTempDirectory("bridge-celestial-");Files.createDirectory(directory.resolve("sprites"));
        try {
            var found=NativeUiExport.collectSprites(manager);Identifier id=Identifier.of("minecraft:textures/environment/celestial/sun.png");
            assertSame(selected,found.get(id));assertEquals("environment/celestial/sun",NativeUiExport.spriteKey(id));
            long[] written={0};var metadata=NativeUiExport.copySprite(directory,found.get(id),written);
            assertEquals(7,metadata.get("width").getAsInt());assertEquals(11,metadata.get("height").getAsInt());
            assertEquals(MobModelExport.sha256(packSun),metadata.get("sha256").getAsString());
            assertArrayEquals(packSun,Files.readAllBytes(directory.resolve(metadata.get("file").getAsString())));
            assertEquals(packSun.length,written[0]);
            NativeUiExport.copySprite(directory,selected,written);
            try(var paths=Files.list(directory.resolve("sprites"))) {assertEquals(1,paths.count());}
        } finally {try(var paths=Files.walk(directory)) {for(var path:paths.sorted(Comparator.reverseOrder()).toList()) Files.delete(path);}}
    }
    @Test public void absentModernAssetsKeepLegacyPackAndExistingHudKeys() throws Exception {
        SelectedResources manager=new SelectedResources();byte[] pixel=png(1,1,0xffffffff);
        manager.put("minecraft:textures/environment/sun.png",pixel);manager.put("minecraft:textures/environment/moon_phases.png",pixel);
        manager.put("minecraft:textures/gui/sprites/hud/hotbar.png",pixel);manager.put("custom:textures/gui/sprites/hud/example.png",pixel);
        manager.put("minecraft:textures/particle/explosion_3.png",pixel);manager.put("minecraft:textures/particle/unrelated.png",pixel);
        var found=NativeUiExport.collectSprites(manager);var keys=new HashSet<String>();found.keySet().forEach(id->keys.add(NativeUiExport.spriteKey(id)));
        assertEquals(Set.of("environment/sun","environment/moon_phases","hud/hotbar","custom:hud/example","particle/explosion_3","particle/unrelated"),keys);
    }
    @Test public void celestialImagesRetainExistingMalformedAndOversizeRejections() throws Exception {
        var directory=Files.createTempDirectory("bridge-celestial-invalid-");Files.createDirectory(directory.resolve("sprites"));
        try {
            assertThrows(IOException.class,()->NativeUiExport.copySprite(directory,new Resource(null,()->new ByteArrayInputStream(new byte[]{1,2,3})),new long[]{0}));
            assertThrows(IOException.class,()->NativeUiExport.copySprite(directory,new Resource(null,()->new ByteArrayInputStream(new byte[4*1024*1024+1])),new long[]{0}));
            try(var paths=Files.list(directory.resolve("sprites"))) {assertEquals(0,paths.count());}
        } finally {Files.delete(directory.resolve("sprites"));Files.delete(directory);}
    }
}

package dev.tsunami.bridge;

import com.google.gson.*;
import java.io.IOException;
import java.util.*;
import org.junit.Test;
import static org.junit.Assert.*;

public class NativeSoundDataTest {
    private static JsonObject definition(String json) {return JsonParser.parseString(json).getAsJsonObject();}
    private static JsonObject sample(String id) {
        JsonObject result=new JsonObject();result.addProperty("file","waves/"+id.substring(id.indexOf(':')+1)+".wav");result.addProperty("sha256","a".repeat(64));return result;
    }
    @FunctionalInterface private interface Checked {void run() throws IOException;}
    private static void rejects(String message,Checked action) throws IOException {
        try {action.run();fail("Malformed sound graph accepted");}catch(IOException expected) {assertTrue(expected.getMessage(),expected.getMessage().contains(message));}
    }

    @Test public void shorthandSoundsUseTheirEventNamespaceAndDefaultVariations() throws Exception {
        List<String> requested=new ArrayList<>();var data=Map.of("pack:effect",definition("{\"sounds\":[\"custom/chime\",\"minecraft:block/stone1\"]}"));
        JsonObject decoded=sample("pack:custom/chime");var variants=NativeSoundData.resolve(data,"pack:effect",id->{requested.add(id);return decoded;});
        assertEquals(List.of("pack:custom/chime","minecraft:block/stone1"),requested);assertEquals(2,variants.size());
        for(var value:variants) {var variant=value.getAsJsonObject();assertEquals(1,variant.get("volume").getAsFloat(),0);assertEquals(1,variant.get("pitch").getAsFloat(),0);assertEquals(1,variant.get("weight").getAsInt());}
        assertFalse("Shared decoded sample metadata is immutable",decoded.has("volume"));
    }

    @Test public void eventReferencePreservesChildSelectionWeightsInsteadOfMultiplyingReferenceWeight() throws Exception {
        var definitions=Map.of(
            "test:root",definition("{\"sounds\":[{\"name\":\"branch\",\"type\":\"event\",\"weight\":999},{\"name\":\"direct\",\"weight\":2}]}"),
            "test:branch",definition("{\"sounds\":[{\"name\":\"one\",\"weight\":3},{\"name\":\"two\",\"weight\":1}]}"));
        var variants=NativeSoundData.resolve(definitions,"test:root",NativeSoundDataTest::sample);
        assertEquals(3,variants.size());int total=0;for(var variant:variants) total+=variant.getAsJsonObject().get("weight").getAsInt();
        assertEquals(6,total);assertEquals(3,variants.get(0).getAsJsonObject().get("weight").getAsInt());assertEquals(1,variants.get(1).getAsJsonObject().get("weight").getAsInt());assertEquals(2,variants.get(2).getAsJsonObject().get("weight").getAsInt());
    }

    @Test public void nestedEventVolumePitchMultiplyWithoutMutatingDefinitions() throws Exception {
        var root=definition("{\"sounds\":[{\"name\":\"branch\",\"type\":\"event\",\"volume\":0.8,\"pitch\":1.2}]}");var branch=definition("{\"sounds\":[{\"name\":\"wave\",\"volume\":0.5,\"pitch\":0.7,\"weight\":3}]}");
        var definitions=Map.of("test:root",root,"test:branch",branch);String before=root.toString()+branch.toString();
        var variant=NativeSoundData.resolve(definitions,"test:root",NativeSoundDataTest::sample).get(0).getAsJsonObject();
        assertEquals(.4,variant.get("volume").getAsFloat(),1e-6);assertEquals(.84,variant.get("pitch").getAsFloat(),1e-6);assertEquals(3,variant.get("weight").getAsInt());
        assertEquals(before,root.toString()+branch.toString());
    }

    @Test public void independentReferencesCanReuseAnEventWhileCyclesAndMissingEventsFail() throws Exception {
        var definitions=new HashMap<String,JsonObject>();definitions.put("test:root",definition("{\"sounds\":[{\"name\":\"branch\",\"type\":\"event\"},{\"name\":\"branch\",\"type\":\"event\"}]}"));definitions.put("test:branch",definition("{\"sounds\":[\"wave\"]}"));
        assertEquals(2,NativeSoundData.resolve(definitions,"test:root",NativeSoundDataTest::sample).size());
        definitions.put("test:branch",definition("{\"sounds\":[{\"name\":\"root\",\"type\":\"event\"}]}"));
        rejects("Cyclic",()->NativeSoundData.resolve(definitions,"test:root",NativeSoundDataTest::sample));
        rejects("missing",()->NativeSoundData.resolve(definitions,"test:unknown",NativeSoundDataTest::sample));
    }

    @Test public void invalidNumericVariationsTypesAndResourceTraversalAreExplicitFailures() throws Exception {
        for(String spec:List.of("{\"name\":\"wave\",\"volume\":-1}","{\"name\":\"wave\",\"pitch\":0}","{\"name\":\"wave\",\"weight\":0}","{\"name\":\"wave\",\"weight\":1025}")) {
            var defs=Map.of("test:event",definition("{\"sounds\":["+spec+"]}"));rejects("variation",()->NativeSoundData.resolve(defs,"test:event",NativeSoundDataTest::sample));
        }
        var invalidType=Map.of("test:event",definition("{\"sounds\":[{\"name\":\"wave\",\"type\":\"custom\"}]}"));rejects("registration type",()->NativeSoundData.resolve(invalidType,"test:event",NativeSoundDataTest::sample));
        var traversal=Map.of("test:event",definition("{\"sounds\":[\"../../outside\"]}"));rejects("resource",()->NativeSoundData.resolve(traversal,"test:event",NativeSoundDataTest::sample));
    }

    @Test public void graphDepthAndVariationCountAreBoundedBeforePublication() throws Exception {
        var deep=new HashMap<String,JsonObject>();for(int i=0;i<18;i++) deep.put("test:e"+i,definition("{\"sounds\":[{\"name\":\"e"+(i+1)+"\",\"type\":\"event\"}]}"));deep.put("test:e18",definition("{\"sounds\":[\"wave\"]}"));
        rejects("deep",()->NativeSoundData.resolve(deep,"test:e0",NativeSoundDataTest::sample));
        JsonObject many=new JsonObject();JsonArray entries=new JsonArray();for(int i=0;i<1025;i++) entries.add("wave"+i);many.add("sounds",entries);
        rejects("variation limit",()->NativeSoundData.resolve(Map.of("test:event",many),"test:event",NativeSoundDataTest::sample));
    }
}

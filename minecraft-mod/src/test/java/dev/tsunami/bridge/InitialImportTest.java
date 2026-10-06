package dev.tsunami.bridge;
import org.junit.Test;
import static org.junit.Assert.*;
public class InitialImportTest {
    @Test public void readyRequiresMatchingServerCommitAndRetries() {
        var flow=new InitialImport(); flow.start("receiver");
        assertEquals("world_begin",flow.request(0)); assertEquals("",flow.request(500_000_000));
        assertEquals("world_begin",flow.request(1_000_000_000));
        flow.observe("receiver","other",true); assertEquals(InitialImport.Phase.BEGIN,flow.phase());
        flow.observe("receiver",flow.id(),false); assertEquals(InitialImport.Phase.COPYING,flow.phase());
        assertEquals("",flow.request(2_000_000_000)); flow.copied();
        assertEquals("world_commit",flow.request(2_000_000_000));
        flow.observe("receiver",flow.id(),false); assertEquals(InitialImport.Phase.COMMIT,flow.phase());
        flow.observe("receiver",flow.id(),true); assertEquals(InitialImport.Phase.READY,flow.phase());
        assertEquals("",flow.request(4_000_000_000L)); assertTrue(flow.ownsWorld());
    }
    @Test public void receiverRestartDoesNotSilentlyOverwriteUEChanges() {
        var flow=new InitialImport(); flow.start("old"); flow.observe("new",flow.id(),false);
        assertEquals(InitialImport.Phase.LOST,flow.phase()); assertEquals("",flow.request(0));
        flow.start("new"); assertEquals(InitialImport.Phase.BEGIN,flow.phase());
        flow.reset(); assertFalse(flow.ownsWorld()); assertEquals("",flow.request(0));
    }
}

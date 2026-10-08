package dev.tsunami.bridge;
import org.junit.Test;
import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;
import static org.junit.Assert.*;
public class DiagnosticArchiveTest {
    @Test public void archiveIsReadableAndExclusive() throws Exception {
        Path dir=Files.createTempDirectory("diagnostic-test");Path file=dir.resolve("capture.zip");
        try {
            try(var zip=new DiagnosticArchive(file)) {zip.put("classes/test.class",new byte[]{(byte)0xca,(byte)0xfe,(byte)0xba,(byte)0xbe,0,0,0,65});zip.put("snapshot.json","{}".getBytes());}
            try(var zip=new ZipFile(file.toFile())) {assertEquals(2,zip.size());assertTrue(DiagnosticArchive.classFile(zip.getInputStream(zip.getEntry("classes/test.class")).readAllBytes()));}
            assertThrows(FileAlreadyExistsException.class,()->new DiagnosticArchive(file));
        } finally {Files.deleteIfExists(file);Files.delete(dir);}
    }
    @Test public void rejectsTraversalDuplicateAndOversize() throws Exception {
        Path file=Files.createTempDirectory("diagnostic-test").resolve("capture.zip");
        try(var zip=new DiagnosticArchive(file)) {
            for(String bad:List.of("../out","/absolute","C:/out","a\\b","a/../b","a//b")) assertThrows(IOException.class,()->zip.put(bad,new byte[0]));
            zip.put("valid",new byte[0]);assertThrows(IOException.class,()->zip.put("valid",new byte[0]));
            assertThrows(IOException.class,()->zip.put("huge",new byte[DiagnosticArchive.ENTRY_LIMIT+1]));
        } finally {Files.deleteIfExists(file);Files.delete(file.getParent());}
    }
    @Test public void streamLimitAndClassMagicAreChecked() throws Exception {
        assertThrows(FileNotFoundException.class,()->DiagnosticArchive.read(null));
        assertThrows(IOException.class,()->DiagnosticArchive.read(new ByteArrayInputStream(new byte[DiagnosticArchive.ENTRY_LIMIT+1])));
        assertFalse(DiagnosticArchive.classFile(new byte[8]));assertFalse(DiagnosticArchive.classFile(new byte[]{(byte)0xca,(byte)0xfe,(byte)0xba,(byte)0xbe}));
        assertEquals("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",DiagnosticArchive.hash(new byte[0]));
    }
    @Test public void indexHasUniqueCombatAiAndRendererClasses() throws Exception {
        String text=new String(DiagnosticArchive.read(getClass().getResourceAsStream("/diagnostics/classes.tsv")),java.nio.charset.StandardCharsets.UTF_8);
        Set<String> names=new HashSet<>(),ids=new HashSet<>();
        for(String line:text.split("\\R")) if(!line.isBlank() && !line.startsWith("#")) {String[] fields=line.split("\t");assertEquals(2,fields.length);assertTrue(ids.add(fields[0]));assertTrue(names.add(fields[1]));}
        assertTrue(names.contains("net/minecraft/entity/LivingEntity"));assertTrue(names.contains("net/minecraft/client/Mouse"));assertTrue(names.contains("net/minecraft/entity/ai/goal/GoalSelector"));assertTrue(names.contains("net/minecraft/entity/ai/control/MoveControl"));assertTrue(names.size()<=768);
    }
}

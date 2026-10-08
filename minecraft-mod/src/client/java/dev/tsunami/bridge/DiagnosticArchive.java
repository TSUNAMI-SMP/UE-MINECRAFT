package dev.tsunami.bridge;

import java.io.*;
import java.nio.file.*;
import java.security.*;
import java.util.*;
import java.util.zip.*;

/** Bounded, exclusive diagnostic ZIP writer; no Minecraft dependencies. */
final class DiagnosticArchive implements AutoCloseable {
    static final int ENTRY_LIMIT=8*1024*1024;
    static final long TOTAL_LIMIT=64L*1024*1024;
    private final ZipOutputStream zip;
    private final Set<String> names=new HashSet<>();
    private long bytes;
    DiagnosticArchive(Path path) throws IOException {zip=new ZipOutputStream(Files.newOutputStream(path,StandardOpenOption.CREATE_NEW));}
    void put(String name,byte[] data) throws IOException {
        if(name.startsWith("/") || name.contains("\\") || name.contains(":") || Arrays.asList(name.split("/",-1)).stream().anyMatch(s->s.isEmpty() || s.equals("..") || s.equals("."))) throw new IOException("Unsafe ZIP path");
        if(data.length>ENTRY_LIMIT || bytes+data.length>TOTAL_LIMIT || names.size()>=2048) throw new IOException("Diagnostic budget exceeded");
        if(!names.add(name)) throw new IOException("Duplicate ZIP entry");
        bytes+=data.length;zip.putNextEntry(new ZipEntry(name));zip.write(data);zip.closeEntry();
    }
    static byte[] read(InputStream input) throws IOException {
        if(input==null) throw new FileNotFoundException("Resource missing");
        try(input) {byte[] data=input.readNBytes(ENTRY_LIMIT+1);if(data.length>ENTRY_LIMIT) throw new IOException("Resource exceeds entry limit");return data;}
    }
    static boolean classFile(byte[] data) {return data.length>=8 && data[0]==(byte)0xca && data[1]==(byte)0xfe && data[2]==(byte)0xba && data[3]==(byte)0xbe;}
    static String hash(byte[] data) {try {return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(data));}catch(NoSuchAlgorithmException e){throw new AssertionError(e);}}
    public void close() throws IOException {zip.close();}
}

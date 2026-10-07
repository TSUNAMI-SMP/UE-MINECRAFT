package dev.tsunami.bridge;

import com.google.gson.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.*;
import java.util.*;

/** File format shared by the native exporter and UE's finite-world loader. No game or network dependency. */
public final class NativeExportData {
    public static final int MAX_ROWS=2_097_152;
    public static final long MAX_WORLD_BYTES=512L*1024*1024;
    private NativeExportData() {}
    public static JsonObject cell(WorldSnapshot.Cell cell,List<WorldSnapshot.Shape> shapes,int[] skyTop) {
        if(cell==null || shapes==null || shapes.size()>512 || skyTop==null || skyTop.length!=64
                || Math.abs((long)cell.x())>3_750_000 || Math.abs((long)cell.y())>3_750_000 || Math.abs((long)cell.z())>3_750_000)
            throw new IllegalArgumentException("Invalid native cell");
        JsonObject result=new JsonObject();result.addProperty("type","cell");
        result.add("cell",array(cell.x(),cell.y(),cell.z()));JsonArray top=new JsonArray();
        for(int value:skyTop) {if(value<0 || value>15) throw new IllegalArgumentException("Invalid sky seed");top.add(value);}result.add("skyTop",top);
        JsonArray palette=new JsonArray(),blocks=new JsonArray();Map<String,Integer> indices=new LinkedHashMap<>();Set<Integer> owners=new HashSet<>();
        for(var shape:shapes) {
            long x=(long)shape.blockX()-cell.x()*8L,y=(long)shape.blockY()-cell.y()*8L,z=(long)shape.blockZ()-cell.z()*8L;
            if(shape.role()!=1 || shape.blockId().isEmpty() || x<0 || x>7 || y<0 || y>7 || z<0 || z>7)
                throw new IllegalArgumentException("Native cell must contain one logical block per voxel");
            int local=(int)(x+(z<<3)+(y<<6));
            if(!owners.add(local)) throw new IllegalArgumentException("Native cell must contain one logical block per voxel");
            JsonArray entry=new JsonArray();entry.add(shape.blockId());entry.add(shape.stateKey());entry.add(shape.color());entry.add(shape.opacity());entry.add(shape.emission());
            Integer index=indices.get(entry.toString());if(index==null) {index=palette.size();indices.put(entry.toString(),index);palette.add(entry);}
            blocks.add(array(local,index,shape.skyLight(),shape.blockLight()));
        }
        result.add("palette",palette);result.add("blocks",blocks);return result;
    }
    public static JsonArray array(double... values) {JsonArray result=new JsonArray();for(double value:values) {if(!Double.isFinite(value)) throw new IllegalArgumentException("Nonfinite coordinate");result.add(value);}return result;}
    public static JsonArray array(int... values) {JsonArray result=new JsonArray();for(int value:values) result.add(value);return result;}
    public static String sha256(Path file) throws IOException {
        try {MessageDigest hash=MessageDigest.getInstance("SHA-256");try(var in=Files.newInputStream(file)) {byte[] buffer=new byte[65536];for(int read;(read=in.read(buffer))>=0;) if(read>0) hash.update(buffer,0,read);}return HexFormat.of().formatHex(hash.digest());}
        catch(NoSuchAlgorithmException impossible) {throw new IllegalStateException(impossible);}
    }
    public static JsonObject reference(Path root,Path file) throws IOException {
        Path base=root.toAbsolutePath().normalize(),path=file.toAbsolutePath().normalize();
        if(!path.startsWith(base) || Files.isSymbolicLink(path) || !Files.isRegularFile(path)) throw new IOException("Asset escapes native bundle");
        // A regular leaf below a symlinked directory can still point outside the export.
        for(Path parent=path.getParent();parent!=null && parent.startsWith(base);parent=parent.getParent())
            if(Files.isSymbolicLink(parent)) throw new IOException("Asset escapes native bundle");
        JsonObject result=new JsonObject();result.addProperty("file",base.relativize(path).toString().replace('\\','/'));
        result.addProperty("sha256",sha256(path));result.addProperty("bytes",Files.size(path));return result;
    }
    public static Path writeManifest(Path root,JsonObject manifest) throws IOException {
        Path file=root.resolve("native_manifest.json");Files.writeString(file,new GsonBuilder().setPrettyPrinting().create().toJson(manifest),StandardCharsets.UTF_8,StandardOpenOption.CREATE_NEW);return file;
    }
}

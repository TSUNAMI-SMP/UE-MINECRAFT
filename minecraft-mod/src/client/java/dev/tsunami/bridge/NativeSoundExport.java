package dev.tsunami.bridge;

import com.google.gson.*;
import java.io.*;
import java.nio.*;
import java.nio.file.*;
import java.util.*;
import java.util.function.Consumer;
import net.minecraft.client.sound.OggAudioStream;

/** Resolves the user's active sounds.json and decodes local Vorbis into bounded PCM16 WAV files. */
public final class NativeSoundExport {
    private final NativeFontExport.Resources resources;private final Path directory;private final Map<String,JsonObject> definitions;
    private final JsonObject sounds=new JsonObject(),excluded=new JsonObject(),blockSounds;private final Map<String,JsonObject> decoded=new HashMap<>();private long totalBytes;
    private NativeSoundExport(NativeFontExport.Resources resources,Path directory,Map<String,JsonObject> definitions,JsonObject blockSounds) {this.resources=resources;this.directory=directory;this.definitions=definitions;this.blockSounds=blockSounds;}
    public static Path export(NativeFontExport.Resources resources,Path root,Map<String,JsonObject> definitions,JsonObject blockSounds,Set<String> requested,Consumer<String> progress) throws IOException {
        Path directory=root.resolve("sounds");Files.createDirectory(directory);Files.createDirectory(directory.resolve("waves"));
        NativeSoundExport job=new NativeSoundExport(resources,directory,definitions,blockSounds);int done=0;
        for(String id:new TreeSet<>(requested)) {
            if(Thread.currentThread().isInterrupted()) throw new IOException("Sound export cancelled");
            try {JsonArray variants=NativeSoundData.resolve(definitions,id,job::wave);if(variants.isEmpty()) throw new IOException("Sound event resolves to no samples");job.sounds.add(id,variants);}
            catch(IOException | RuntimeException error) {job.excluded.addProperty(id,error.getMessage()==null ? error.getClass().getSimpleName() : error.getMessage());}
            progress.accept("音声書き出し "+(++done)+"/"+requested.size());
        }
        if(!job.sounds.has("minecraft:block.stone.break") || !job.sounds.has("minecraft:ui.button.click")) throw new IOException("Required native block/UI sounds could not be exported; see active resource pack");
        JsonObject manifest=new JsonObject();manifest.addProperty("kind","sounds");manifest.addProperty("version",1);manifest.add("sounds",job.sounds);manifest.add("blockSounds",blockSounds);manifest.add("excluded",job.excluded);
        Path path=directory.resolve("manifest.json");Files.writeString(path,new GsonBuilder().setPrettyPrinting().create().toJson(manifest),StandardOpenOption.CREATE_NEW);return path;
    }
    private JsonObject wave(String id) throws IOException {
        if(decoded.containsKey(id)) return decoded.get(id);if(decoded.size()>=4096) throw new IOException("Sound sample count limit");
        int colon=id.indexOf(':');byte[] ogg=resources.read(id.substring(0,colon+1)+"sounds/"+id.substring(colon+1)+".ogg");
        if(ogg==null || ogg.length>4*1024*1024) throw new IOException("Sound sample missing/oversized: "+id);
        byte[] pcm;int channels,rate;
        try(var input=new OggAudioStream(new ByteArrayInputStream(ogg))) {
            channels=input.getFormat().getChannels();rate=(int)input.getFormat().getSampleRate();if((channels!=1 && channels!=2) || rate<8000 || rate>96000) throw new IOException("Unsupported sound format");
            ByteArrayOutputStream samples=new ByteArrayOutputStream();
            try {while(input.read(value->{if(samples.size()>10*1024*1024-2) throw new IllegalStateException("Decoded sample exceeds 10 MiB");int sample=Math.max(-32768,Math.min(32767,(int)(value*32767)));samples.write(sample&255);samples.write((sample>>8)&255);})) {if(Thread.currentThread().isInterrupted()) throw new IOException("Sound export cancelled");}}
            catch(IllegalStateException tooLarge) {throw new IOException(tooLarge.getMessage(),tooLarge);}pcm=samples.toByteArray();
        }
        if(pcm.length==0 || pcm.length%(channels*2)!=0) throw new IOException("Invalid decoded sound sample");
        ByteBuffer buffer=ByteBuffer.allocate(44+pcm.length).order(ByteOrder.LITTLE_ENDIAN);buffer.put("RIFF".getBytes(java.nio.charset.StandardCharsets.US_ASCII)).putInt(36+pcm.length).put("WAVEfmt ".getBytes(java.nio.charset.StandardCharsets.US_ASCII));
        buffer.putInt(16).putShort((short)1).putShort((short)channels).putInt(rate).putInt(rate*channels*2).putShort((short)(channels*2)).putShort((short)16).put("data".getBytes(java.nio.charset.StandardCharsets.US_ASCII)).putInt(pcm.length).put(pcm);
        byte[] wav=buffer.array();totalBytes+=wav.length;if(totalBytes>256L*1024*1024) throw new IOException("Native sound byte budget exceeded");
        String hash=MobModelExport.sha256(wav),file="waves/"+hash+".wav";if(!Files.exists(directory.resolve(file))) Files.write(directory.resolve(file),wav,StandardOpenOption.CREATE_NEW);
        JsonObject entry=new JsonObject();entry.addProperty("file",file);entry.addProperty("sha256",hash);entry.addProperty("sampleRate",rate);entry.addProperty("channels",channels);decoded.put(id,entry);return entry;
    }
}

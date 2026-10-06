package dev.tsunami.bridge;

import java.nio.file.*;
import java.util.*;
import java.io.IOException;
import java.util.function.Consumer;

/** Read-only resource access plus bounded files in a new export directory; daemon never changes a world. */
public final class TextureExportJob implements AutoCloseable {
    private volatile boolean running=true,cancelled;
    private volatile String status="テクスチャ書き出し中";
    private final Thread worker;
    public TextureExportJob(TextureExport.Resources resources,List<TextureExport.Block> blocks,Path root,Consumer<String> complete) {
        worker=new Thread(()->{
            try {
                if(blocks.size()>4096) throw new IOException("Block registry exceeds export limit (4096)");
                Files.createDirectories(root);
                Path directory=root.resolve("textures-"+java.time.LocalDateTime.now().toString().replace(':','-')+"-"+UUID.randomUUID().toString().substring(0,8));
                TextureExport export=new TextureExport(resources,directory); int done=0;
                for(var block:blocks) {
                    if(cancelled) { status="書き出しを中止しました"; return; }
                    export.export(block); status="テクスチャ書き出し "+(++done)+"/"+blocks.size()+" / 対応="+export.exported();
                }
                Path manifest=export.finish(); status="完了: "+export.exported()+"種類 / 未対応="+export.skipped()+" / "+manifest;
                if(!cancelled) complete.accept(status);
            } catch(IOException | RuntimeException e) { status="書き出し失敗: "+e.getMessage(); if(!cancelled) complete.accept(status); }
            finally { running=false; }
        },"UE-Bridge-Texture-Export"); worker.setDaemon(true); worker.start();
    }
    public boolean running() { return running; }
    public String status() { return status; }
    @Override public void close() { cancelled=true; worker.interrupt(); }
}

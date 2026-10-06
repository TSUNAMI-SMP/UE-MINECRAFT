package dev.tsunami.bridge;

import java.util.UUID;

/** One-shot import handshake; status confirms server state, ACK alone does not finish an import. */
public final class InitialImport {
    public enum Phase { IDLE, BEGIN, COPYING, COMMIT, READY, LOST }
    private Phase phase=Phase.IDLE;
    private String id="", receiver="";
    private long nextRequest;
    public void start(String receiverId) { id=UUID.randomUUID().toString(); receiver=receiverId; phase=Phase.BEGIN; nextRequest=0; }
    public void reset() { phase=Phase.IDLE; id=receiver=""; nextRequest=0; }
    public void observe(String receiverId,String importId,boolean sealed) {
        if(phase==Phase.IDLE || phase==Phase.LOST) return;
        if(!receiver.equals(receiverId)) { phase=Phase.LOST; return; }
        if(!id.equals(importId)) return;
        if(sealed) phase=Phase.READY;
        else if(phase==Phase.BEGIN) phase=Phase.COPYING;
    }
    public void copied() { if(phase==Phase.COPYING) { phase=Phase.COMMIT; nextRequest=0; } }
    public String request(long now) {
        if((phase!=Phase.BEGIN && phase!=Phase.COMMIT) || now<nextRequest) return "";
        nextRequest=now+1_000_000_000L; return phase==Phase.BEGIN ? "world_begin" : "world_commit";
    }
    public String id() { return id; }
    public Phase phase() { return phase; }
    public boolean ownsWorld() { return phase!=Phase.IDLE; }
}

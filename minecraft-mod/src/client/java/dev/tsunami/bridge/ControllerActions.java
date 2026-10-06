package dev.tsunami.bridge;

/** Button edges with creative-mode repeat; screens/disconnects never enqueue clicks. */
public final class ControllerActions {
    private long nextBreak, nextPlace;
    private boolean breaking, placing;
    public record Buttons(boolean breaking, boolean placing) {}
    public Buttons sample(boolean active, boolean attack, boolean use, long now) {
        if (!active) { reset(); return new Buttons(false,false); }
        boolean sendBreak=attack && (!breaking || now>=nextBreak);
        boolean sendPlace=use && (!placing || now>=nextPlace);
        if(sendBreak) nextBreak=now+200_000_000L;
        if(sendPlace) nextPlace=now+200_000_000L;
        breaking=attack; placing=use; return new Buttons(sendBreak,sendPlace);
    }
    public void reset() { breaking=placing=false; nextBreak=nextPlace=0; }
}

package dev.tsunami.bridge;

/** Configured jump-key edges; no native flight/world mutation. */
public final class ControllerFlight {
    private boolean pressed,flying,wasAirborne;
    private long lastPress=-1;
    public boolean sample(boolean active,boolean creative,boolean jump,boolean grounded,long now) {
        if(!active || !creative) { reset();pressed=jump;return false; }
        if(!grounded && flying) wasAirborne=true;
        if(grounded && flying && wasAirborne) {flying=false;wasAirborne=false;}
        if(jump && !pressed) {
            if(lastPress>=0 && now-lastPress<=350_000_000L) {flying=!flying;wasAirborne=false;lastPress=-1;}
            else lastPress=now;
        }
        pressed=jump;return flying;
    }
    public void reset() {pressed=flying=wasAirborne=false;lastPress=-1;}
}

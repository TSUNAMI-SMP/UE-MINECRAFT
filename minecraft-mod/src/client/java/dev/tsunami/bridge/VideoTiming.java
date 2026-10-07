package dev.tsunami.bridge;

import java.util.Arrays;

/** Rolling measurements of distinct received/display-submitted UE frames, never Minecraft's HUD FPS. */
final class VideoTiming {
    private static final int CAPACITY=512;
    private final long[] receives=new long[CAPACITY],displays=new long[CAPACITY];
    private final double[] delays=new double[CAPACITY];
    private int receivedCount,displayCount,delayCount;
    private long lastDisplayed=-1;
    synchronized void received(long now){receives[(receivedCount++)%CAPACITY]=now;}
    synchronized boolean displayed(long sequence,long now,double delay) {
        if(sequence==lastDisplayed) return false;
        lastDisplayed=sequence;displays[(displayCount++)%CAPACITY]=now;
        if(Double.isFinite(delay) && delay>=0 && delay<=10000) delays[(delayCount++)%CAPACITY]=delay;
        return true;
    }
    synchronized double receivedFps(long now){return fps(receives,receivedCount,now);}
    synchronized double displayedFps(long now){return fps(displays,displayCount,now);}
    private static double fps(long[] entries,int count,long now) {
        int available=Math.min(count,CAPACITY),used=0;long first=now,last=0;
        for(int i=0;i<available;i++) {long t=entries[i];if(t<=now && now-t<=2_000_000_000L){used++;first=Math.min(first,t);last=Math.max(last,t);}}
        return used<2 || last==first ? 0 : (used-1)*1_000_000_000.0/(last-first);
    }
    synchronized double percentile(double proportion) {
        int count=Math.min(delayCount,CAPACITY);if(count==0)return -1;
        double[] copy=Arrays.copyOf(delays,count);Arrays.sort(copy);
        return copy[Math.max(0,Math.min(count-1,(int)Math.ceil(proportion*count)-1))];
    }
}

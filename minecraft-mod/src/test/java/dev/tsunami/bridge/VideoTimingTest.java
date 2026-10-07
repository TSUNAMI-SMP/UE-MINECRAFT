package dev.tsunami.bridge;

import org.junit.Test;
import static org.junit.Assert.*;

public class VideoTimingTest {
    @Test public void receivedAndDistinctDisplayedFramesHaveSeparateRates() {
        var stats=new VideoTiming();long start=10_000_000_000L;
        for(int i=0;i<41;i++) {
            long now=start+i*50_000_000L;stats.received(now);
            if(i%2==0) {assertTrue(stats.displayed(i,now,i));assertFalse(stats.displayed(i,now+1,i));}
        }
        assertEquals(20,stats.receivedFps(start+2_000_000_000L),.0001);
        assertEquals(10,stats.displayedFps(start+2_000_000_000L),.0001);
        assertEquals(0,stats.displayedFps(start+5_000_000_000L),0);
    }
    @Test public void percentileUsesBoundedRecentSamplesAndRejectsUnknownDelays() {
        var stats=new VideoTiming();assertEquals(-1,stats.percentile(.95),0);
        for(int i=0;i<512;i++)stats.displayed(i,i*1_000_000L,i+1);
        assertEquals(256,stats.percentile(.5),0);assertEquals(487,stats.percentile(.95),0);
        stats.displayed(512,1_000_000_000L,1024);
        assertEquals(257,stats.percentile(.5),0);
        stats.displayed(513,1_000_000_001L,-1);stats.displayed(514,1_000_000_002L,Double.NaN);
        assertEquals(257,stats.percentile(.5),0);
    }
}

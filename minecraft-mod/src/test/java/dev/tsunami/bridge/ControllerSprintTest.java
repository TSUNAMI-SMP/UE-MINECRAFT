package dev.tsunami.bridge;

import org.junit.Test;
import static org.junit.Assert.*;

public class ControllerSprintTest {
    private static boolean sample(ControllerSprint sprint, boolean forward, long time) {
        return sprint.sample(true,forward,false,false,false,time);
    }
    private static void startDoubleTap(ControllerSprint sprint) {
        assertFalse(sample(sprint,true,0));
        assertFalse(sample(sprint,false,50_000_000));
        assertTrue(sample(sprint,true,100_000_000));
    }

    @Test public void doubleTapStartsAndHoldsUntilForwardRelease() {
        var sprint = new ControllerSprint();
        startDoubleTap(sprint);
        assertTrue(sample(sprint,true,2_000_000_000));
        assertFalse(sample(sprint,false,2_050_000_000));
        assertFalse(sample(sprint,true,2_100_000_000));
    }
    @Test public void holdingForwardDoesNotCountAsTwoTaps() {
        var sprint = new ControllerSprint();
        assertFalse(sample(sprint,true,0));
        assertFalse(sample(sprint,true,50_000_000));
        assertFalse(sample(sprint,true,100_000_000));
    }
    @Test public void tapWindowUsesElapsedTimeAndIncludesVanillaBoundary() {
        var sprint = new ControllerSprint();
        assertFalse(sample(sprint,true,0));
        assertFalse(sample(sprint,false,10_000_000));
        assertTrue(sample(sprint,true,ControllerSprint.DOUBLE_TAP_NANOS));
        sprint.reset();
        assertFalse(sample(sprint,true,0));
        assertFalse(sample(sprint,false,10_000_000));
        assertFalse(sample(sprint,true,ControllerSprint.DOUBLE_TAP_NANOS+1));
    }
    @Test public void configuredSprintKeyRequiresForwardAndCanLatchAfterRelease() {
        var sprint = new ControllerSprint();
        assertFalse(sprint.sample(true,false,false,true,false,0));
        assertTrue(sprint.sample(true,true,false,true,false,1));
        assertTrue(sprint.sample(true,true,false,false,false,2));
        assertFalse(sprint.sample(true,false,false,false,false,3));
    }
    @Test public void backAndSneakCancelSprintAndTapHistory() {
        for(boolean back:new boolean[]{true,false}) {
            var sprint = new ControllerSprint();
            startDoubleTap(sprint);
            assertFalse(sprint.sample(true,true,back,true,!back,120_000_000));
            assertFalse(sample(sprint,true,150_000_000));
        }
    }
    @Test public void disabledAuthorityAndScreensClearHistoryBeforeHeldKeysResume() {
        var sprint = new ControllerSprint();
        assertFalse(sample(sprint,true,0));
        assertFalse(sprint.sample(false,false,false,false,false,50_000_000));
        assertFalse(sample(sprint,true,100_000_000));
        assertFalse(sprint.sample(false,true,false,true,false,150_000_000));
        assertFalse(sample(sprint,true,200_000_000));
    }
    @Test public void resetOnDisconnectOrControlChangeCannotResumeOldSprint() {
        var sprint = new ControllerSprint();
        startDoubleTap(sprint);
        sprint.reset();
        assertFalse(sample(sprint,true,150_000_000));
    }
    @Test public void clockRollbackCannotCreateSprintOrResurrectTap() {
        var sprint = new ControllerSprint();
        startDoubleTap(sprint);
        assertFalse(sample(sprint,true,50_000_000));
        assertFalse(sample(sprint,true,60_000_000));
    }
}

package dev.tsunami.bridge;
import org.junit.Test;
import static org.junit.Assert.*;

public class IgnitionTrackerTest {
    @Test public void onlyClickedPrimedTntMatchesOnce() {
        IgnitionTracker tracker = new IgnitionTracker();
        assertNull(tracker.primed(0.5, 0, 0.5, 0));
        tracker.click(0, 0, 0, 0); assertNull(tracker.primed(5, 0, 5, 1));
        assertEquals(new IgnitionTracker.Position(0, 0, 0), tracker.primed(0.5, 0.2, 0.5, 100));
        assertNull(tracker.primed(0.5, 0.2, 0.5, 200));
    }
    @Test public void tracksMultipleClicksAndNegativeCoordinates() {
        IgnitionTracker tracker = new IgnitionTracker(); tracker.click(-1, 60, -1, 0); tracker.click(1, 60, 1, 0);
        assertEquals(new IgnitionTracker.Position(1, 60, 1), tracker.primed(1.5, 60, 1.5, 100));
        assertEquals(new IgnitionTracker.Position(-1, 60, -1), tracker.primed(-0.5, 60, -0.5, 100));
    }
    @Test public void expiredClicksAndWorldResetNeverFire() {
        IgnitionTracker tracker = new IgnitionTracker(); tracker.click(0, 0, 0, 0);
        assertNull(tracker.primed(0.5, 0, 0.5, 2_000_000_000L));
        tracker.click(0, 0, 0, 3_000_000_000L); tracker.clear();
        assertNull(tracker.primed(0.5, 0, 0.5, 3_000_000_001L));
    }
}

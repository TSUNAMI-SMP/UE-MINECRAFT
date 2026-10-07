package dev.tsunami.bridge;

import org.junit.Test;
import static org.junit.Assert.*;

public class VanillaLightingStateTest {
    @Test public void invalidOrExtremeEnvironmentCannotPoisonGpuConstants() {
        assertEquals(0,VanillaLightingState.finite(Double.NaN,0,4),0);
        assertEquals(0,VanillaLightingState.finite(Double.POSITIVE_INFINITY,0,4),0);
        assertEquals(4,VanillaLightingState.finite(100,0,4),0);
        assertEquals(0,VanillaLightingState.finite(-100,0,4),0);
        assertEquals(.25,VanillaLightingState.finite(.25,0,1),0);
    }
}

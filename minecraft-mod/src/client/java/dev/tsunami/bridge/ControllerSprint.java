package dev.tsunami.bridge;

/** UE input state only: never changes the frozen Minecraft player's sprint flag or attributes. */
public final class ControllerSprint {
    // Vanilla's forward double-tap interval is seven client ticks.
    static final long DOUBLE_TAP_NANOS = 350_000_000L;
    private boolean sprinting, forwardHeld, tapArmed, sampled;
    private long lastTap, lastSample;

    /** Keys are resolved by Minecraft's configured bindings, not physical key codes. */
    public boolean sample(boolean allowed, boolean forward, boolean back, boolean sprintKey,
                          boolean sneak, long now) {
        if (!allowed || back || sneak || (sampled && now < lastSample)) {
            reset();
            return false;
        }
        sampled = true;
        lastSample = now;
        if (tapArmed && now - lastTap > DOUBLE_TAP_NANOS) tapArmed = false;
        if (!forward) sprinting = false;
        else {
            if (!forwardHeld) {
                if (tapArmed && now - lastTap <= DOUBLE_TAP_NANOS) {
                    sprinting = true;
                    tapArmed = false;
                } else {
                    lastTap = now;
                    tapArmed = true;
                }
            }
            // Like vanilla, releasing the sprint key alone does not end an active forward sprint.
            if (sprintKey) sprinting = true;
        }
        forwardHeld = forward;
        return sprinting;
    }

    /** Called on authority changes, import/control commands, disconnect, and disabled input. */
    public void reset() {
        sprinting = forwardHeld = tapArmed = sampled = false;
        lastTap = lastSample = 0;
    }
}

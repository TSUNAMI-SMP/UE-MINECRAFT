package dev.tsunami.bridge;
import org.junit.Test;
import static org.junit.Assert.*;
public class ControllerFlightTest {
    @Test public void configuredJumpEdgesToggleCreativeFlightWithoutGroundAckRace() {
        var flight=new ControllerFlight();
        assertFalse(flight.sample(true,true,true,true,0));
        assertFalse(flight.sample(true,true,false,true,50_000_000));
        assertTrue(flight.sample(true,true,true,true,100_000_000));
        assertTrue(flight.sample(true,true,true,true,108_000_000));
        assertTrue(flight.sample(true,true,false,false,200_000_000));
        assertFalse(flight.sample(true,true,false,true,300_000_000));
    }
    @Test public void survivalModeImmediatelyRevokesPermissionAndCannotToggle() {
        var flight=new ControllerFlight();flight.sample(true,true,true,false,0);flight.sample(true,true,false,false,50_000_000);
        assertTrue(flight.sample(true,true,true,false,100_000_000));
        assertFalse(flight.sample(true,false,true,false,110_000_000));
        flight.sample(true,false,false,false,150_000_000);assertFalse(flight.sample(true,false,true,false,200_000_000));
    }
    @Test public void heldJumpAndSlowRepeatedJumpDoNotToggleFlight() {
        var flight=new ControllerFlight();flight.sample(true,true,true,false,0);
        assertFalse(flight.sample(true,true,true,false,100_000_000));
        flight.sample(true,true,false,false,400_000_000);assertFalse(flight.sample(true,true,true,false,500_000_000));
    }
    @Test public void secondDoublePressDisablesFlightInMidAir() {
        var flight=new ControllerFlight();flight.sample(true,true,true,false,0);flight.sample(true,true,false,false,50_000_000);
        assertTrue(flight.sample(true,true,true,false,100_000_000));flight.sample(true,true,false,false,150_000_000);
        assertTrue(flight.sample(true,true,true,false,500_000_000));flight.sample(true,true,false,false,550_000_000);
        assertFalse(flight.sample(true,true,true,false,600_000_000));
    }
}

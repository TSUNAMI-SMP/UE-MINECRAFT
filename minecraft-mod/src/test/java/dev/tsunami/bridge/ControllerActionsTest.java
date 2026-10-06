package dev.tsunami.bridge;
import org.junit.Test;
import static org.junit.Assert.*;
public class ControllerActionsTest {
    @Test public void clicksRepeatAtCreativeRateWithoutFlooding() {
        var actions=new ControllerActions();
        assertEquals(new ControllerActions.Buttons(true,true),actions.sample(true,true,true,1));
        assertEquals(new ControllerActions.Buttons(false,false),actions.sample(true,true,true,199_000_001));
        assertEquals(new ControllerActions.Buttons(true,true),actions.sample(true,true,true,200_000_001));
    }
    @Test public void releasedButtonsAndClosedScreenStartNewEdges() {
        var actions=new ControllerActions();
        assertTrue(actions.sample(true,true,false,1).breaking());
        actions.sample(true,false,false,2);
        assertTrue(actions.sample(true,true,false,3).breaking());
        assertEquals(new ControllerActions.Buttons(false,false),actions.sample(false,true,true,4));
        assertEquals(new ControllerActions.Buttons(true,true),actions.sample(true,true,true,5));
    }
}

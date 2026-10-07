package dev.tsunami.bridge;

import org.junit.Test;
import static org.junit.Assert.*;

public class ItemDropLedgerTest {
    private static final class Inventory {
        int count=64,capacity=64,debits;
        final ItemDropLedger<String> ledger=new ItemDropLedger<>((token,amount)->{int accepted=Math.min(amount,capacity-count);count+=accepted;return accepted;});
        void drop(String id,int amount) {assertTrue(ledger.reserve(id,"diamond",amount,()->{assertTrue(count>=amount);count-=amount;debits++;}));}
    }
    @Test public void repeatedDropAndPickupNeverDuplicateInventory() {
        Inventory i=new Inventory();i.drop("a",7);i.drop("a",7);assertEquals(57,i.count);assertEquals(1,i.debits);
        assertEquals(0,i.ledger.result("a",0,"spawned",7).accepted());
        assertEquals(7,i.ledger.result("a",1,"pickup",7).accepted());
        assertEquals(7,i.ledger.result("a",1,"pickup",7).accepted());assertEquals(64,i.count);assertEquals(0,i.ledger.outstanding());
    }
    @Test public void reusedRevisionCannotChangePreviouslyAppliedOperation() {
        Inventory i=new Inventory();i.drop("a",8);i.ledger.result("a",0,"spawned",8);
        assertNull(i.ledger.result("a",0,"rejected",8));
        i.ledger.result("a",1,"pickup",3);
        assertNull(i.ledger.result("a",1,"pickup",5));assertNull(i.ledger.result("a",1,"expired",3));
        assertEquals(59,i.count);assertEquals(5,i.ledger.outstanding());
    }
    @Test public void fullInventoryPickupRetainsDropAndPartialPickupCreditsExactAmount() {
        Inventory i=new Inventory();i.drop("a",12);i.ledger.result("a",0,"spawned",12);
        i.count=63;assertEquals(1,i.ledger.result("a",1,"pickup",12).accepted());assertEquals(11,i.ledger.outstanding());
        assertEquals(0,i.ledger.result("a",2,"pickup",11).accepted());assertEquals(64,i.count);
        i.count=58;assertEquals(6,i.ledger.result("a",3,"pickup",11).accepted());assertEquals(5,i.ledger.outstanding());
    }
    @Test public void rejectionRefundIsIdempotentEvenWithOverflow() {
        Inventory i=new Inventory();i.drop("a",10);i.count=60;
        assertEquals(4,i.ledger.result("a",0,"rejected",10).accepted());assertEquals(6,i.ledger.outstanding());
        assertEquals(4,i.ledger.result("a",0,"rejected",10).accepted());assertEquals(64,i.count);
        i.count=54;i.ledger.retryRefunds();assertEquals(60,i.count);assertEquals(0,i.ledger.outstanding());
    }
    @Test public void disconnectFencesNewPickupsBeforeRefundAndOldReceiptsRemainStable() {
        Inventory i=new Inventory();i.drop("a",10);i.ledger.result("a",0,"spawned",10);
        assertEquals(3,i.ledger.result("a",1,"pickup",3).accepted());
        i.ledger.refundAll();assertEquals(64,i.count);
        assertNull(i.ledger.result("a",2,"pickup",7));assertEquals(3,i.ledger.result("a",1,"pickup",3).accepted());assertEquals(64,i.count);
        i.ledger.refundAll();assertEquals(64,i.count);
    }
    @Test public void expiryConsumesOnlyExistingEscrowAndCannotResurrect() {
        Inventory i=new Inventory();i.drop("a",4);i.ledger.result("a",0,"spawned",4);
        assertNull(i.ledger.result("a",1,"expired",5));assertEquals(4,i.ledger.outstanding());
        assertEquals(0,i.ledger.result("a",1,"expired",4).accepted());assertEquals(0,i.ledger.outstanding());
        i.ledger.refundAll();assertEquals(60,i.count);assertNull(i.ledger.result("a",2,"pickup",4));
    }
    @Test public void malformedOutOfOrderAndUnknownTransactionsDoNotCredit() {
        Inventory i=new Inventory();i.drop("a",4);
        assertNull(i.ledger.result("b",0,"spawned",4));assertNull(i.ledger.result("a",1,"pickup",4));
        i.ledger.result("a",0,"spawned",4);
        assertNull(i.ledger.result("a",2,"pickup",4));assertNull(i.ledger.result("a",1,"pickup",5));assertNull(i.ledger.result("a",1,"pickup",0));
        assertEquals(60,i.count);assertEquals(4,i.ledger.outstanding());
    }
    @Test public void balancesSaveOnlyUncreditedAmountsForAtomicPlayerRecovery() {
        Inventory i=new Inventory();i.drop("a",9);i.ledger.result("a",0,"spawned",9);i.ledger.result("a",1,"pickup",2);
        assertEquals(java.util.List.of(new ItemDropLedger.Balance<>("diamond",7)),i.ledger.balances());
        assertEquals(57,i.count);int restored=i.ledger.balances().stream().mapToInt(ItemDropLedger.Balance::count).sum();assertEquals(64,i.count+restored);
    }
}

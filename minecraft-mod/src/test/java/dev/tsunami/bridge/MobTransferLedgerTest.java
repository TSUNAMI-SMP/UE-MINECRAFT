package dev.tsunami.bridge;

import java.util.UUID;
import org.junit.Test;
import static org.junit.Assert.*;

public final class MobTransferLedgerTest {
    private MobTransferLedger.Transfer first(MobTransferLedger ledger) {return ledger.transfers().iterator().next();}
    @Test public void onlyItsOwnSuccessfulReceiptGrantsSourceLease() {
        var ledger=new MobTransferLedger();UUID a=UUID.randomUUID(),b=UUID.randomUUID();
        assertTrue(ledger.add(a));assertTrue(ledger.add(b));
        var transfer=first(ledger);ledger.sent(transfer,"accepted-event");
        assertTrue(ledger.accepted().isEmpty());
        ledger.receipt(transfer,true,"ready");assertEquals(java.util.Set.of(a),ledger.accepted());
        assertEquals(1,ledger.count(MobTransferLedger.State.QUEUED));
    }
    @Test public void rejectedModelNeverSuspendsTheSource() {
        var ledger=new MobTransferLedger();ledger.add(UUID.randomUUID());var transfer=first(ledger);
        ledger.sent(transfer,"rejected-event");ledger.receipt(transfer,false,"appearance_not_in_assigned_palette");
        assertEquals(MobTransferLedger.State.REJECTED,transfer.state);assertTrue(ledger.accepted().isEmpty());
        ledger.receipt(transfer,true,"stale-ack");assertTrue(ledger.accepted().isEmpty());
    }
    @Test public void boundedTimeoutRetriesKeepTheSourceUuidAndNeverClaimOwnership() {
        var ledger=new MobTransferLedger();UUID id=UUID.randomUUID();ledger.add(id);var transfer=first(ledger);
        for(int attempt=0;attempt<3;attempt++) {ledger.sent(transfer,"attempt-"+attempt);ledger.expired(transfer,123L);}
        assertEquals(id,transfer.id);assertEquals(MobTransferLedger.State.EXPIRED,transfer.state);
        assertEquals(3,transfer.attempts);assertTrue(ledger.accepted().isEmpty());assertFalse(ledger.add(id));
    }
    @Test public void acceptedUuidIsATombstoneUntilTheImportChanges() {
        var ledger=new MobTransferLedger();UUID id=UUID.randomUUID();ledger.add(id);var transfer=first(ledger);
        ledger.sent(transfer,"event");ledger.receipt(transfer,true,"ready");
        assertFalse(ledger.add(id));assertTrue(ledger.contains(id));ledger.clear();assertTrue(ledger.add(id));
    }
    @Test public void historyIsBoundedWithoutForgettingAcceptedSourceIds() {
        var ledger=new MobTransferLedger();for(int i=0;i<MobTransferLedger.MAX_HISTORY;i++) assertTrue(ledger.add(new UUID(0,i+1)));
        assertFalse(ledger.add(UUID.randomUUID()));assertEquals(MobTransferLedger.MAX_HISTORY,ledger.count(MobTransferLedger.State.QUEUED));
    }
}

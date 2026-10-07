package dev.tsunami.bridge;

import java.util.*;
import java.util.function.ToIntBiFunction;

/** Server-thread escrow. UE requests cannot create inventory items: every credit is
 * bounded by a previous debit, and a repeated revision returns its original receipt. */
public final class ItemDropLedger<T> {
    public record Balance<T>(T token,int count) {}
    public record Receipt(int accepted,boolean closed) {}
    private static final int LIMIT=512;
    private record Applied(String action,int count,Receipt receipt) {}
    private static final class Entry<T> {
        final T token;int remaining,revision;boolean spawned,closing;
        final LinkedHashMap<Integer,Applied> receipts=new LinkedHashMap<>();
        Entry(T value,int amount) {token=value;remaining=amount;}
    }
    private final LinkedHashMap<String,Entry<T>> entries=new LinkedHashMap<>();
    private final ToIntBiFunction<T,Integer> credit;
    public ItemDropLedger(ToIntBiFunction<T,Integer> inserter) {credit=Objects.requireNonNull(inserter);}
    public boolean reserve(String id,T token,int count,Runnable debit) {
        if(entries.containsKey(id)) return true;
        if(entries.size()>=LIMIT || count<1 || count>99 || token==null) return false;
        debit.run();entries.put(id,new Entry<>(token,count));return true;
    }
    public Receipt result(String id,int revision,String action,int count) {
        Entry<T> entry=entries.get(id);if(entry==null || revision<0 || revision>100000 || count<0 || count>99) return null;
        Applied existing=entry.receipts.get(revision);if(existing!=null) return existing.action().equals(action) && existing.count()==count ? existing.receipt() : null;
        if(entry.closing) return null;
        int accepted=0;
        if(revision==0 && entry.receipts.isEmpty()) {
            if("spawned".equals(action) && count==entry.remaining) entry.spawned=true;
            else if("rejected".equals(action)) {entry.closing=true;accepted=insert(entry.token,entry.remaining);entry.remaining-=accepted;}
            else return null;
        } else {
            if(!entry.spawned || revision!=entry.revision+1 || count>entry.remaining || count<1) return null;
            if("pickup".equals(action)) {accepted=insert(entry.token,count);entry.remaining-=accepted;}
            else if("expired".equals(action) && count==entry.remaining) {entry.remaining=0;entry.closing=true;}
            else return null;
            entry.revision=revision;
        }
        Receipt receipt=new Receipt(accepted,entry.remaining==0);
        entry.receipts.put(revision,new Applied(action,count,receipt));
        if(entry.receipts.size()>128) entry.receipts.remove(entry.receipts.keySet().iterator().next());
        return receipt;
    }
    private int insert(T token,int count) {return Math.max(0,Math.min(count,credit.applyAsInt(token,count)));}
    /** Fence outstanding revisions before refunding; delayed pickup requests cannot credit twice. */
    public void refundAll() {
        for(Entry<T> entry:entries.values()) {entry.closing=true;int accepted=insert(entry.token,entry.remaining);entry.remaining-=accepted;}
    }
    public void retryRefunds() {
        for(Entry<T> entry:entries.values()) if(entry.closing && entry.remaining>0) entry.remaining-=insert(entry.token,entry.remaining);
    }
    public List<Balance<T>> balances() {
        List<Balance<T>> out=new ArrayList<>();for(Entry<T> entry:entries.values()) if(entry.remaining>0) out.add(new Balance<>(entry.token,entry.remaining));return List.copyOf(out);
    }
    public int outstanding() {int result=0;for(Entry<T> entry:entries.values()) result+=entry.remaining;return result;}
    public int transactionCount() {return entries.size();}
}

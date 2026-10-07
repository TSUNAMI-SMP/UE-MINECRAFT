package dev.tsunami.bridge;

import java.util.LinkedHashMap;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;

/** Per-source delivery ownership, independent of Minecraft and the transport's other events. */
final class MobTransferLedger {
    static final int MAX_HISTORY = 4096, MAX_ATTEMPTS = 3;
    enum State { QUEUED, IN_FLIGHT, ACCEPTED, REJECTED, EXPIRED }
    static final class Transfer {
        final UUID id;
        State state = State.QUEUED;
        String eventId = "", reason = "";
        int attempts;
        long retryAt;
        Transfer(UUID id) { this.id = id; }
    }
    private final LinkedHashMap<UUID, Transfer> transfers = new LinkedHashMap<>();
    boolean contains(UUID id) { return transfers.containsKey(id); }
    boolean add(UUID id) {
        if (id == null || contains(id) || transfers.size() >= MAX_HISTORY) return false;
        transfers.put(id, new Transfer(id)); return true;
    }
    Iterable<Transfer> transfers() { return transfers.values(); }
    Set<UUID> history() {return Set.copyOf(transfers.keySet());}
    void sent(Transfer transfer, String eventId) {
        if (transfer.state != State.QUEUED || eventId == null || eventId.isEmpty()) throw new IllegalStateException("Mob transfer is not queued");
        transfer.eventId = eventId; transfer.attempts++; transfer.state = State.IN_FLIGHT;
    }
    void receipt(Transfer transfer, boolean accepted, String reason) {
        if (transfer.state != State.IN_FLIGHT) return;
        transfer.state = accepted ? State.ACCEPTED : State.REJECTED;
        transfer.reason = reason == null ? "" : reason;
    }
    void expired(Transfer transfer, long now) {
        if (transfer.state != State.IN_FLIGHT) return;
        transfer.eventId = "";
        transfer.reason = "receipt_timeout";
        if (transfer.attempts < MAX_ATTEMPTS) { transfer.state = State.QUEUED; transfer.retryAt = now + 500_000_000L; }
        else transfer.state = State.EXPIRED;
    }
    Set<UUID> accepted() {
        return transfers.values().stream().filter(t -> t.state == State.ACCEPTED).map(t -> t.id).collect(Collectors.toUnmodifiableSet());
    }
    int count(State state) { return (int)transfers.values().stream().filter(t -> t.state == state).count(); }
    void clear() { transfers.clear(); }
}

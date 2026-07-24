package systems.zlink.framework.runtime;

import java.time.Duration;
import java.time.Instant;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.zip.CRC32C;
import systems.zlink.framework.locations.*;

public final class InMemoryRelocationStore implements ZLinkRelocationStore {
    private final Map<String, Entry> values = new ConcurrentHashMap<>();

    public InMemoryRelocationStore() {
    }

    @Override
    public CompletionStage<ZLinkRelocationStored> put(
        byte[] payload,
        Duration retention,
        ZLinkStoreCancellation cancellation) {
        String reference = UUID.randomUUID().toString();
        Instant now = Instant.now();
        Instant expiresAt = now.plus(retention);
        byte[] copy = payload.clone();
        values.put(reference, new Entry(copy, expiresAt));
        CRC32C checksum = new CRC32C();
        checksum.update(copy);
        return CompletableFuture.completedFuture(
            new ZLinkRelocationStored(
                reference, checksum.getValue(), expiresAt, now));
    }

    @Override
    public CompletionStage<ZLinkRelocationReadResult> get(
        String reference,
        ZLinkStoreCancellation cancellation) {
        Entry entry = values.get(reference);
        Instant now = Instant.now();
        if (entry == null || !entry.expiresAt().isAfter(now)) {
            values.remove(reference);
            return CompletableFuture.completedFuture(
                new ZLinkRelocationMissing());
        }
        return CompletableFuture.completedFuture(
            new ZLinkRelocationFound(entry.payload()));
    }

    @Override
    public CompletionStage<ZLinkRelocationRenewResult> renew(
        String reference,
        Duration retention,
        ZLinkStoreCancellation cancellation) {
        Entry entry = values.get(reference);
        if (entry == null) {
            return CompletableFuture.completedFuture(
                new ZLinkRelocationRenewMissing());
        }
        Instant expiresAt = Instant.now().plus(retention);
        values.put(reference, new Entry(entry.payload(), expiresAt));
        return CompletableFuture.completedFuture(
            new ZLinkRelocationRenewed(expiresAt, Instant.now()));
    }

    @Override
    public CompletionStage<ZLinkRelocationDeleteResult> delete(
        String reference,
        ZLinkStoreCancellation cancellation) {
        return CompletableFuture.completedFuture(
            values.remove(reference) == null
                ? ZLinkRelocationDeleteResult.MISSING
                : ZLinkRelocationDeleteResult.DELETED);
    }

    private record Entry(byte[] payload, Instant expiresAt) {
        private Entry {
            payload = payload.clone();
        }
        @Override public byte[] payload() {
            return payload.clone();
        }
    }
}

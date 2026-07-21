package systems.zlink.e2e.runtimemonitoring.service.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CompletionStage;

public final class MulticastGate {
    private final ConcurrentHashMap<String, CompletableFuture<Void>> gates =
        new ConcurrentHashMap<>();

    public void block(String spotRid) {
        gates.computeIfAbsent(spotRid, ignored -> new CompletableFuture<>());
    }

    public void release(String spotRid) {
        CompletableFuture<Void> gate = gates.remove(spotRid);
        if (gate != null) {
            gate.complete(null);
        }
    }

    public CompletionStage<Void> await(String spotRid) {
        CompletableFuture<Void> gate = gates.get(spotRid);
        return gate == null ? CompletableFuture.completedFuture(null) : gate;
    }
}

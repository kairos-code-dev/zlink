package systems.zlink.framework.runtime.internal.relocation;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;

/**
 * Seals a fixed inventory of serial lanes as one generation.
 *
 * <p>The exact {@link Seal} instance is the fence. A failed partial seal is
 * rolled back before this method returns, so a caller never observes a
 * partially sealed participant inventory.
 */
public final class ZLinkCompositeRelocationBarrier {
    private long nextGeneration = 1L;
    private Seal active;

    public synchronized Optional<Seal> trySeal(
        Map<String, ZLinkAsyncSerialQueue> lanes) {
        if (active != null) {
            return Optional.empty();
        }
        if (lanes == null || lanes.isEmpty()) {
            throw new IllegalArgumentException(
                "at least one relocation lane is required");
        }
        if (nextGeneration == Long.MAX_VALUE) {
            throw new IllegalStateException(
                "composite relocation generation exhausted");
        }
        LinkedHashMap<String, ZLinkAsyncSerialQueue> laneSnapshot =
            new LinkedHashMap<>();
        LinkedHashMap<String, ZLinkAsyncSerialQueue.RelocationSeal> seals =
            new LinkedHashMap<>();
        try {
            for (Map.Entry<String, ZLinkAsyncSerialQueue> lane
                : lanes.entrySet()) {
                String laneId = requireLaneId(lane.getKey());
                ZLinkAsyncSerialQueue queue =
                    java.util.Objects.requireNonNull(
                        lane.getValue(), "relocation lane");
                if (laneSnapshot.putIfAbsent(laneId, queue) != null) {
                    throw new IllegalArgumentException(
                        "duplicate relocation lane: " + laneId);
                }
                Optional<ZLinkAsyncSerialQueue.RelocationSeal> sealed =
                    queue.trySealRelocation();
                if (sealed.isEmpty()) {
                    rollback(laneSnapshot, seals);
                    return Optional.empty();
                }
                seals.put(laneId, sealed.get());
            }
        } catch (RuntimeException failure) {
            rollback(laneSnapshot, seals);
            throw failure;
        }
        Seal result = new Seal(
            nextGeneration++,
            java.util.Collections.unmodifiableMap(
                new LinkedHashMap<>(laneSnapshot)),
            java.util.Collections.unmodifiableMap(
                new LinkedHashMap<>(seals)));
        active = result;
        return Optional.of(result);
    }

    public synchronized boolean abort(Seal seal) {
        if (seal == null || seal != active) {
            return false;
        }
        List<String> laneIds =
            new ArrayList<>(seal.lanes.keySet());
        java.util.Collections.reverse(laneIds);
        boolean restored = true;
        for (String laneId : laneIds) {
            restored &= seal.lanes.get(laneId).abortRelocation(
                seal.seals.get(laneId));
        }
        if (!restored) {
            throw new IllegalStateException(
                "composite relocation abort lost a lane fence");
        }
        active = null;
        return true;
    }

    public synchronized Optional<Map<String, List<
        ZLinkAsyncSerialQueue.QueuedRecord>>> commit(Seal seal) {
        if (seal == null || seal != active) {
            return Optional.empty();
        }
        LinkedHashMap<String, List<ZLinkAsyncSerialQueue.QueuedRecord>>
            held = new LinkedHashMap<>();
        for (Map.Entry<String, ZLinkAsyncSerialQueue> lane
            : seal.lanes.entrySet()) {
            List<ZLinkAsyncSerialQueue.QueuedRecord> records =
                lane.getValue().commitRelocation(
                    seal.seals.get(lane.getKey()))
                    .orElseThrow(() -> new IllegalStateException(
                        "composite relocation commit lost a lane fence"));
            held.put(lane.getKey(), records);
        }
        active = null;
        return Optional.of(Map.copyOf(held));
    }

    public synchronized <T> CompletionStage<T> runCapture(
        Seal seal,
        Supplier<CompletionStage<T>> capture) {
        if (seal == null || seal != active) {
            throw new IllegalStateException(
                "capture requires the active relocation barrier generation");
        }
        return java.util.Objects.requireNonNull(
            capture.get(), "capture result");
    }

    private static void rollback(
        Map<String, ZLinkAsyncSerialQueue> lanes,
        Map<String, ZLinkAsyncSerialQueue.RelocationSeal> seals) {
        List<String> laneIds = new ArrayList<>(seals.keySet());
        java.util.Collections.reverse(laneIds);
        for (String laneId : laneIds) {
            if (!lanes.get(laneId).abortRelocation(seals.get(laneId))) {
                throw new IllegalStateException(
                    "partial relocation seal rollback lost a lane fence");
            }
        }
    }

    private static String requireLaneId(String laneId) {
        if (laneId == null || laneId.isBlank()) {
            throw new IllegalArgumentException(
                "relocation lane id is required");
        }
        return laneId;
    }

    public static final class Seal {
        private final long generation;
        private final Map<String, ZLinkAsyncSerialQueue> lanes;
        private final Map<String, ZLinkAsyncSerialQueue.RelocationSeal> seals;

        private Seal(
            long generation,
            Map<String, ZLinkAsyncSerialQueue> lanes,
            Map<String, ZLinkAsyncSerialQueue.RelocationSeal> seals) {
            this.generation = generation;
            this.lanes = lanes;
            this.seals = seals;
        }

        public long generation() {
            return generation;
        }

        public List<String> laneIds() {
            return List.copyOf(lanes.keySet());
        }
    }
}

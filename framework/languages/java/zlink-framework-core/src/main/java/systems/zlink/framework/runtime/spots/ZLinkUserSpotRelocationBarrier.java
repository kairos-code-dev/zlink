package systems.zlink.framework.runtime.spots;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.runtime.internal.relocation
    .ZLinkCompositeRelocationBarrier;

/**
 * Owns the source-side all-lane barrier for one User Spot aggregate.
 */
final class ZLinkUserSpotRelocationBarrier {
    private final DefaultSpotContext context;
    private final ZLinkActorSessionCoordinator actors;
    private final ZLinkCompositeRelocationBarrier barrier =
        new ZLinkCompositeRelocationBarrier();
    private Seal active;

    ZLinkUserSpotRelocationBarrier(
        DefaultSpotContext context,
        ZLinkActorSessionCoordinator actors) {
        this.context = java.util.Objects.requireNonNull(
            context, "context");
        this.actors = java.util.Objects.requireNonNull(
            actors, "actors");
    }

    synchronized Optional<Seal> trySeal() {
        if (active != null) {
            return Optional.empty();
        }
        byte[] timerEnvelope =
            context.freezeTimerRelocationEnvelope();
        List<String> participantActorIds =
            actors.actorIdsInSpot(context.spotId());
        LinkedHashMap<String, ZLinkAsyncSerialQueue> localLanes =
            new LinkedHashMap<>(context.relocationLanes());
        Optional<ZLinkCompositeRelocationBarrier.Seal> localSeal =
            barrier.trySeal(localLanes);
        if (localSeal.isEmpty()) {
            context.resumeTimersAfterRelocationAbort();
            return Optional.empty();
        }
        LinkedHashMap<String, ZLinkAsyncSerialQueue.RelocationSeal>
            actorSeals = new LinkedHashMap<>();
        for (String actorId : participantActorIds) {
            Optional<ZLinkAsyncSerialQueue.RelocationSeal> actorSeal =
                actors.trySealActorRelocation(actorId);
            if (actorSeal.isEmpty()) {
                rollback(localSeal.get(), actorSeals);
                context.resumeTimersAfterRelocationAbort();
                return Optional.empty();
            }
            actorSeals.put(actorId, actorSeal.get());
        }
        List<String> currentActorIds =
            actors.actorIdsInSpot(context.spotId());
        if (!participantActorIds.equals(currentActorIds)) {
            rollback(localSeal.get(), actorSeals);
            context.resumeTimersAfterRelocationAbort();
            return Optional.empty();
        }
        active = new Seal(
            localSeal.get(),
            timerEnvelope.clone(),
            participantActorIds,
            java.util.Collections.unmodifiableMap(actorSeals));
        return Optional.of(active);
    }

    synchronized <T> CompletionStage<T> runCapture(
        Seal seal,
        Supplier<CompletionStage<T>> capture) {
        requireActive(seal);
        return barrier.runCapture(seal.composite, capture);
    }

    synchronized boolean abort(Seal seal) {
        if (seal == null || seal != active) {
            return false;
        }
        List<String> actorIds =
            new java.util.ArrayList<>(seal.actorSeals.keySet());
        java.util.Collections.reverse(actorIds);
        for (String actorId : actorIds) {
            if (!actors.abortActorRelocation(
                actorId, seal.actorSeals.get(actorId))) {
                throw new IllegalStateException(
                    "User Spot barrier abort lost Actor lane: "
                        + actorId);
            }
        }
        if (!barrier.abort(seal.composite)) {
            throw new IllegalStateException(
                "User Spot barrier abort lost local lane");
        }
        active = null;
        context.resumeTimersAfterRelocationAbort();
        return true;
    }

    synchronized Optional<Committed> commit(Seal seal) {
        if (seal == null || seal != active) {
            return Optional.empty();
        }
        LinkedHashMap<String, List<ZLinkAsyncSerialQueue.QueuedRecord>>
            heldIngress = new LinkedHashMap<>();
        for (Map.Entry<String, ZLinkAsyncSerialQueue.RelocationSeal> actor
            : seal.actorSeals.entrySet()) {
            heldIngress.put(
                "actor:" + actor.getKey(),
                actors.commitActorRelocation(
                    actor.getKey(), actor.getValue())
                    .orElseThrow(() -> new IllegalStateException(
                        "User Spot barrier commit lost Actor lane: "
                            + actor.getKey())));
        }
        Map<String, List<ZLinkAsyncSerialQueue.QueuedRecord>> localHeld =
            barrier.commit(seal.composite)
                .orElseThrow(() -> new IllegalStateException(
                    "User Spot barrier commit lost local lane"));
        heldIngress.putAll(localHeld);
        active = null;
        return Optional.of(new Committed(
            seal.generation(),
            seal.timerEnvelope(),
            seal.participantActorIds(),
            heldIngress));
    }

    private void rollback(
        ZLinkCompositeRelocationBarrier.Seal localSeal,
        Map<String, ZLinkAsyncSerialQueue.RelocationSeal> actorSeals) {
        List<String> actorIds =
            new java.util.ArrayList<>(actorSeals.keySet());
        java.util.Collections.reverse(actorIds);
        for (String actorId : actorIds) {
            if (!actors.abortActorRelocation(
                actorId, actorSeals.get(actorId))) {
                throw new IllegalStateException(
                    "partial User Spot barrier rollback lost Actor lane: "
                        + actorId);
            }
        }
        if (!barrier.abort(localSeal)) {
            throw new IllegalStateException(
                "partial User Spot barrier rollback lost local lane");
        }
    }

    private void requireActive(Seal seal) {
        if (seal == null || seal != active) {
            throw new IllegalStateException(
                "capture requires the active User Spot barrier generation");
        }
    }

    static final class Seal {
        private final ZLinkCompositeRelocationBarrier.Seal composite;
        private final byte[] timerEnvelope;
        private final List<String> participantActorIds;
        private final Map<
            String, ZLinkAsyncSerialQueue.RelocationSeal> actorSeals;

        private Seal(
            ZLinkCompositeRelocationBarrier.Seal composite,
            byte[] timerEnvelope,
            List<String> participantActorIds,
            Map<String, ZLinkAsyncSerialQueue.RelocationSeal> actorSeals) {
            this.composite = composite;
            this.timerEnvelope = timerEnvelope;
            this.participantActorIds = List.copyOf(
                participantActorIds);
            this.actorSeals = actorSeals;
        }

        long generation() {
            return composite.generation();
        }

        byte[] timerEnvelope() {
            return timerEnvelope.clone();
        }

        List<String> participantActorIds() {
            return participantActorIds;
        }
    }

    record Committed(
        long generation,
        byte[] timerEnvelope,
        List<String> participantActorIds,
        Map<String, List<ZLinkAsyncSerialQueue.QueuedRecord>> heldIngress) {
        Committed {
            timerEnvelope = timerEnvelope.clone();
            participantActorIds = List.copyOf(participantActorIds);
            heldIngress = Map.copyOf(heldIngress);
        }

        @Override
        public byte[] timerEnvelope() {
            return timerEnvelope.clone();
        }
    }
}

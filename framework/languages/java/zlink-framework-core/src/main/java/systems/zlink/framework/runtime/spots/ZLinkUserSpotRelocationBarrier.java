package systems.zlink.framework.runtime.spots;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import java.util.function.Predicate;
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
        return trySeal(ignored -> true);
    }

    synchronized Optional<Seal> trySeal(Predicate<Preview> admission) {
        java.util.Objects.requireNonNull(admission, "admission");
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
        Map<String, List<ZLinkAsyncSerialQueue.QueuedRecord>> captured =
            captureRecords(localSeal.get(), actorSeals);
        boolean admitted;
        try {
            admitted = admission.test(new Preview(
                timerEnvelope,
                participantActorIds,
                captured));
        } catch (RuntimeException failure) {
            rollback(localSeal.get(), actorSeals);
            context.resumeTimersAfterRelocationAbort();
            throw failure;
        }
        if (!admitted) {
            rollback(localSeal.get(), actorSeals);
            context.resumeTimersAfterRelocationAbort();
            return Optional.empty();
        }
        active = new Seal(
            localSeal.get(),
            timerEnvelope.clone(),
            participantActorIds,
            java.util.Collections.unmodifiableMap(actorSeals),
            captured);
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

    synchronized Optional<Map<String, List<
        ZLinkAsyncSerialQueue.QueuedRecord>>> freezeIngress(Seal seal) {
        if (seal == null || seal != active) {
            return Optional.empty();
        }
        LinkedHashMap<String, List<ZLinkAsyncSerialQueue.QueuedRecord>> held =
            new LinkedHashMap<>();
        for (Map.Entry<String, ZLinkAsyncSerialQueue.RelocationSeal> actor
            : seal.actorSeals.entrySet()) {
            held.put(
                "actor:" + actor.getKey(),
                actors.freezeActorRelocationIngress(
                    actor.getKey(), actor.getValue())
                    .orElseThrow(() -> new IllegalStateException(
                        "User Spot barrier freeze lost Actor lane: "
                            + actor.getKey())));
        }
        held.putAll(barrier.freezeIngress(seal.composite)
            .orElseThrow(() -> new IllegalStateException(
                "User Spot barrier freeze lost local lane")));
        return Optional.of(java.util.Collections.unmodifiableMap(held));
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

    private Map<String, List<ZLinkAsyncSerialQueue.QueuedRecord>>
        captureRecords(
            ZLinkCompositeRelocationBarrier.Seal localSeal,
            Map<String, ZLinkAsyncSerialQueue.RelocationSeal> actorSeals) {
        LinkedHashMap<String, List<ZLinkAsyncSerialQueue.QueuedRecord>> result =
            new LinkedHashMap<>(barrier.captured(localSeal)
                .orElseThrow(() -> new IllegalStateException(
                    "User Spot local relocation seal is not active")));
        actorSeals.forEach((actorId, actorSeal) -> result.put(
            "actor:" + actorId,
            actorSeal.captured()));
        return java.util.Collections.unmodifiableMap(result);
    }

    static final class Seal {
        private final ZLinkCompositeRelocationBarrier.Seal composite;
        private final byte[] timerEnvelope;
        private final List<String> participantActorIds;
        private final Map<
            String, ZLinkAsyncSerialQueue.RelocationSeal> actorSeals;
        private final Map<String, List<ZLinkAsyncSerialQueue.QueuedRecord>>
            capturedRecords;

        private Seal(
            ZLinkCompositeRelocationBarrier.Seal composite,
            byte[] timerEnvelope,
            List<String> participantActorIds,
            Map<String, ZLinkAsyncSerialQueue.RelocationSeal> actorSeals,
            Map<String, List<ZLinkAsyncSerialQueue.QueuedRecord>>
                capturedRecords) {
            this.composite = composite;
            this.timerEnvelope = timerEnvelope;
            this.participantActorIds = List.copyOf(
                participantActorIds);
            this.actorSeals = actorSeals;
            this.capturedRecords = capturedRecords;
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

        Map<String, List<ZLinkAsyncSerialQueue.QueuedRecord>>
            capturedRecords() {
            return capturedRecords;
        }
    }

    record Preview(
        byte[] timerEnvelope,
        List<String> participantActorIds,
        Map<String, List<ZLinkAsyncSerialQueue.QueuedRecord>> capturedRecords) {
        Preview {
            timerEnvelope = timerEnvelope.clone();
            participantActorIds = List.copyOf(participantActorIds);
            capturedRecords = Map.copyOf(capturedRecords);
        }

        @Override public byte[] timerEnvelope() {
            return timerEnvelope.clone();
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

package systems.zlink.framework.runtime.spots;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.function.Function;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.internal.locations
    .ZLinkServiceRelocationEnvelopeCodec;
import systems.zlink.framework.spots.ZLinkSpot;

/** Projects one User Spot aggregate onto canonical relocation-envelope-v1. */
final class ZLinkCanonicalUserSpotRelocationEnvelope {
    private ZLinkCanonicalUserSpotRelocationEnvelope() {
    }

    static byte[] encode(
        ZLinkUserSpotAggregateStagingOwner.Request request,
        UUID relocationId,
        long expectedAuthorityOwnerGeneration,
        List<ZLinkSpotRetireControl.ParticipantFence> participants) {
        Objects.requireNonNull(request, "request");
        Objects.requireNonNull(relocationId, "relocationId");
        List<ZLinkSpotRetireControl.ParticipantFence> inventory =
            List.copyOf(participants);
        Map<String, ZLinkUserSpotAggregateStagingOwner.ActorParticipant> actors =
            new java.util.HashMap<>();
        request.actors().forEach(actor -> actors.put(actor.actorId(), actor));
        List<ZLinkSpotTimerRelocationEnvelope.CanonicalTimer> timers =
            ZLinkSpotTimerRelocationEnvelope.canonicalize(
                request.timerEnvelope());
        List<Journal> journal = new ArrayList<>();
        Map<Long, Long> boundaries = new java.util.HashMap<>();
        for (var lane : request.acceptedJournal().entrySet()) {
            long participantId = participantId(inventory, lane.getKey());
            for (var record : lane.getValue()) {
                journal.add(new Journal(
                    participantId, record.sequence(), record.payload()));
                boundaries.merge(
                    participantId, record.sequence(), Math::max);
            }
        }
        journal.sort(Comparator
            .comparingLong(Journal::participantId)
            .thenComparingLong(Journal::sequence));
        long spotParticipant = participantId(inventory, "spot");
        List<PendingTimer> pendingTimers = new ArrayList<>();
        long nextSequence = boundaries.getOrDefault(spotParticipant, 0L);
        for (var timer : timers) {
            if (timer.pending() == null) {
                continue;
            }
            nextSequence = Math.incrementExact(nextSequence);
            pendingTimers.add(new PendingTimer(nextSequence, timer));
        }
        boundaries.put(spotParticipant, nextSequence);

        Writer writer = new Writer();
        writer.u64(relocationId.getMostSignificantBits());
        writer.u64(relocationId.getLeastSignificantBits());
        Writer object = new Writer();
        object.text8(request.spotId());
        object.u64(request.objectGeneration());
        object.u64(expectedAuthorityOwnerGeneration);
        writer.u8(2);
        writer.body16(object);
        writer.u64(1);
        writer.u32(inventory.size());
        for (int index = 0; index < inventory.size(); index++) {
            var participant = inventory.get(index);
            long id = index + 1L;
            byte[] state;
            boolean hasState = participant.restoreSnapshot();
            if (participant.objectKind() == 2) {
                state = request.spotState();
            } else {
                var actor = actors.get(participant.objectId());
                if (actor == null) {
                    throw invalid("Actor inventory differs from captured state");
                }
                state = actor.state();
            }
            writer.u64(id);
            writer.u8(hasState ? 1 : 0);
            Writer stateBody = new Writer();
            if (hasState) {
                stateBody.bytes64(state);
            }
            writer.body64(stateBody);
        }
        writer.u32(inventory.size());
        for (int index = 0; index < inventory.size(); index++) {
            long id = index + 1L;
            writer.u64(id);
            writer.u64(boundaries.getOrDefault(id, 0L));
            writer.u64(0);
        }
        writer.u32(journal.size());
        for (Journal entry : journal) {
            writer.u64(entry.participantId());
            writer.u64(entry.sequence());
            writer.raw(entry.record());
        }
        writer.u32(timers.size());
        for (var timer : timers) {
            writer.u64(spotParticipant);
            writer.text8(timer.name());
            writer.text8(timer.handlerType());
            writer.u64(timer.periodMilliseconds());
            writer.u8(timer.overrunPolicy());
            writer.u64(timer.maxCatchUpTicks());
            writer.u8(timer.stopOnUnhandledException() ? 1 : 0);
            writer.u64(timer.lastCompletedDeliveryIndex());
            writer.u64(timer.lastCompletedScheduledIndex());
            writer.u64(timer.nextScheduledAtUnixMilliseconds());
        }
        writer.u32(pendingTimers.size());
        for (PendingTimer pending : pendingTimers) {
            var timer = pending.timer();
            var tick = timer.pending();
            writer.u64(spotParticipant);
            writer.u64(pending.sequence());
            writer.text8(timer.name());
            writer.u64(tick.deliveryIndex());
            writer.u64(tick.scheduledIndex());
            writer.u64(tick.scheduledAtUnixMilliseconds());
            writer.u64(tick.skippedTicks());
        }
        writer.u32(0);
        byte[] encoded = writer.bytes();
        ZLinkServiceRelocationEnvelopeCodec.decode(encoded);
        return encoded;
    }

    static ZLinkUserSpotAggregateStagingOwner.Request decode(
        byte[] encoded,
        RoutingId targetNodeRid,
        Function<String, Class<? extends ZLinkSpot<?>>> spotTypes,
        ZLinkSpotRetireControl.StageRequest stage) {
        var root = ZLinkServiceRelocationEnvelopeCodec.decode(encoded);
        if (root.relocationHigh() != stage.fence().aggregateId()
                .getMostSignificantBits()
            || root.relocationLow() != stage.fence().aggregateId()
                .getLeastSignificantBits()
            || root.object().kind() != 2
            || !root.object().objectId().equals(stage.spotId())) {
            throw invalid("canonical relocation identity differs from stage");
        }
        List<ZLinkSpotRetireControl.ParticipantFence> inventory =
            stage.participants();
        if (root.applicationStates().size() != inventory.size()
            || root.participantProgress().size() != inventory.size()) {
            throw invalid("canonical participant inventory is incomplete");
        }
        Map<Long, ZLinkServiceRelocationEnvelopeCodec.ApplicationState> states =
            new java.util.HashMap<>();
        root.applicationStates().forEach(value -> states.put(
            value.participantId(), value));
        List<ZLinkUserSpotAggregateStagingOwner.ActorParticipant> actors =
            new ArrayList<>();
        byte[] spotState = null;
        long spotGeneration = 0;
        long spotParticipant = 0;
        Map<Long, String> lanes = new java.util.HashMap<>();
        for (int index = 0; index < inventory.size(); index++) {
            long id = index + 1L;
            var participant = inventory.get(index);
            var state = states.get(id);
            if (state == null
                || state.hasState() != participant.restoreSnapshot()) {
                throw invalid("canonical participant state policy differs");
            }
            if (participant.objectKind() == 2) {
                if (!participant.objectId().equals(stage.spotId())) {
                    throw invalid("canonical Spot inventory differs");
                }
                spotState = state.payload();
                spotGeneration = participant.objectGeneration();
                spotParticipant = id;
                lanes.put(id, "spot");
            } else {
                actors.add(new ZLinkUserSpotAggregateStagingOwner.ActorParticipant(
                    participant.objectId(),
                    participant.stableType(),
                    state.payload(),
                    participant.restoreSnapshot(),
                    new ZLinkBackendActorRef(
                        targetNodeRid,
                        participant.objectId(),
                        participant.objectGeneration())));
                lanes.put(id, "actor:" + participant.objectId());
            }
        }
        if (spotState == null) {
            throw invalid("canonical Spot participant is missing");
        }
        LinkedHashMap<String, List<ZLinkAsyncSerialQueue.QueuedRecord>> journal =
            new LinkedHashMap<>();
        for (var entry : root.journal()) {
            String lane = lanes.get(entry.participantId());
            if (lane == null) {
                throw invalid("canonical journal participant is unknown");
            }
            journal.computeIfAbsent(lane, ignored -> new ArrayList<>())
                .add(new ZLinkAsyncSerialQueue.QueuedRecord(
                    entry.sequence(), entry.rawEntry()));
        }
        Map<String, ZLinkServiceRelocationEnvelopeCodec.PendingTimerTick>
            pendingByName = new java.util.HashMap<>();
        for (var pending : root.pendingTimerTicks()) {
            if (pending.participantId() != spotParticipant) {
                throw invalid("Actor timer is not supported");
            }
            pendingByName.put(pending.timerName(), pending);
        }
        final long canonicalSpotParticipant = spotParticipant;
        List<ZLinkSpotTimerRelocationEnvelope.CanonicalTimer> timers =
            root.timerRegistrations().stream().map(value -> {
                if (value.participantId() != canonicalSpotParticipant) {
                    throw invalid("Actor timer is not supported");
                }
                var pending = pendingByName.get(value.name());
                return new ZLinkSpotTimerRelocationEnvelope.CanonicalTimer(
                    value.name(), value.handlerType(),
                    value.periodMilliseconds(), value.overrunPolicy(),
                    value.maxCatchUpTicks(), value.stopOnUnhandledException(),
                    value.lastCompletedDeliveryIndex(),
                    value.lastCompletedScheduledIndex(),
                    value.nextScheduledAtUnixMilliseconds(),
                    pending == null ? null
                        : new ZLinkSpotTimerRelocationEnvelope.CanonicalPending(
                            pending.deliveryIndex(), pending.scheduledIndex(),
                            pending.scheduledAtUnixMilliseconds(),
                            pending.skippedTicks()));
            }).toList();
        Class<? extends ZLinkSpot<?>> spotType = spotTypes.apply(
            stage.stableType());
        if (spotType == null) {
            throw invalid("target does not register User Spot stable type");
        }
        LinkedHashMap<String, List<ZLinkAsyncSerialQueue.QueuedRecord>> frozen =
            new LinkedHashMap<>();
        journal.forEach((lane, records) -> frozen.put(lane, List.copyOf(records)));
        return new ZLinkUserSpotAggregateStagingOwner.Request(
            spotType, stage.stableType(), stage.spotId(), spotGeneration,
            spotState, stage.restoreSpotSnapshot(),
            ZLinkSpotTimerRelocationEnvelope.encodeCanonical(timers),
            actors, frozen);
    }

    private static long participantId(
        List<ZLinkSpotRetireControl.ParticipantFence> inventory,
        String lane) {
        String actorId = lane.startsWith("actor:") ? lane.substring(6) : null;
        for (int index = 0; index < inventory.size(); index++) {
            var value = inventory.get(index);
            if (actorId == null && value.objectKind() == 2
                || actorId != null && value.objectKind() == 1
                    && value.objectId().equals(actorId)) {
                return index + 1L;
            }
        }
        throw invalid("accepted journal lane is outside participant inventory");
    }

    private static IllegalArgumentException invalid(String message) {
        return new IllegalArgumentException(message);
    }

    private record Journal(long participantId, long sequence, byte[] record) {
        private Journal { record = record.clone(); }
    }
    private record PendingTimer(
        long sequence,
        ZLinkSpotTimerRelocationEnvelope.CanonicalTimer timer) {
    }

    private static final class Writer {
        private final ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        private final DataOutputStream output = new DataOutputStream(bytes);
        void u8(long value) { write(() -> output.writeByte((int) value)); }
        void u32(long value) { write(() -> output.writeInt((int) value)); }
        void u64(long value) { write(() -> output.writeLong(value)); }
        void raw(byte[] value) { write(() -> output.write(value)); }
        void text8(String value) {
            byte[] encoded = value.getBytes(StandardCharsets.UTF_8);
            if (encoded.length < 1 || encoded.length > 255) throw invalid("text8");
            u8(encoded.length); raw(encoded);
        }
        void bytes64(byte[] value) { u64(value.length); raw(value); }
        void body16(Writer body) { u32As16(body.bytes().length); raw(body.bytes()); }
        void body64(Writer body) { u64(body.bytes().length); raw(body.bytes()); }
        private void u32As16(int value) {
            write(() -> output.writeShort(value));
        }
        byte[] bytes() { return bytes.toByteArray(); }
        private void write(Io operation) {
            try { operation.run(); }
            catch (IOException impossible) { throw new IllegalStateException(impossible); }
        }
    }
    @FunctionalInterface private interface Io { void run() throws IOException; }
}

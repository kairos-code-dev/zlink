package systems.zlink.framework.runtime.spots;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.EOFException;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.function.Function;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.spots.ZLinkSpot;

/** Canonical immutable payload for one User Spot aggregate relocation. */
final class ZLinkUserSpotRelocationEnvelope {
    private static final int MAGIC = 0x5a4c5545; // ZLUE
    private static final int VERSION = 1;
    private static final int MAX_TEXT_BYTES = 4096;
    private static final int MAX_PARTICIPANTS = 65_536;
    private static final int MAX_LANES = 65_536;
    private static final int MAX_RECORDS = 1_000_000;
    private static final int MAX_COMPONENT_BYTES = 256 * 1024 * 1024;

    private ZLinkUserSpotRelocationEnvelope() {
    }

    static byte[] encode(ZLinkUserSpotAggregateStagingOwner.Request request) {
        Objects.requireNonNull(request, "request");
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DataOutputStream output = new DataOutputStream(bytes);
            output.writeInt(MAGIC);
            output.writeByte(VERSION);
            text(output, request.spotStableType());
            text(output, request.spotId());
            output.writeLong(request.objectGeneration());
            output.writeBoolean(request.restoreSpotSnapshot());
            component(output, request.spotState());
            component(output, request.timerEnvelope());
            List<ZLinkUserSpotAggregateStagingOwner.ActorParticipant> actors =
                request.actors().stream()
                    .sorted((left, right) -> compareUtf8(
                        left.actorId(), right.actorId()))
                    .toList();
            if (actors.size() > MAX_PARTICIPANTS) {
                throw invalid("Actor participant count exceeds its bound");
            }
            output.writeInt(actors.size());
            for (var actor : actors) {
                text(output, actor.actorId());
                text(output, actor.actorType());
                output.writeLong(actor.preparedActorRef().generation());
                output.writeBoolean(actor.restoreSnapshot());
                component(output, actor.state());
            }
            List<Map.Entry<String, List<ZLinkAsyncSerialQueue.QueuedRecord>>>
                lanes = request.acceptedJournal().entrySet().stream()
                    .sorted((left, right) -> compareUtf8(
                        left.getKey(), right.getKey()))
                    .toList();
            if (lanes.size() > MAX_LANES) {
                throw invalid("accepted journal lane count exceeds its bound");
            }
            output.writeInt(lanes.size());
            int records = 0;
            for (var lane : lanes) {
                text(output, lane.getKey());
                records = Math.addExact(records, lane.getValue().size());
                if (records > MAX_RECORDS) {
                    throw invalid("accepted journal record count exceeds its bound");
                }
                output.writeInt(lane.getValue().size());
                for (var record : lane.getValue()) {
                    output.writeLong(record.sequence());
                    component(output, record.payload());
                }
            }
            output.flush();
            return bytes.toByteArray();
        } catch (IOException failure) {
            throw new IllegalStateException(
                "User Spot relocation envelope could not be encoded",
                failure);
        }
    }

    static ZLinkUserSpotAggregateStagingOwner.Request decode(
        byte[] encoded,
        RoutingId targetNodeRid,
        Function<String, Class<? extends ZLinkSpot<?>>> spotTypes) {
        Objects.requireNonNull(encoded, "encoded");
        Objects.requireNonNull(targetNodeRid, "targetNodeRid");
        Objects.requireNonNull(spotTypes, "spotTypes");
        try {
            DataInputStream input = new DataInputStream(
                new ByteArrayInputStream(encoded));
            if (input.readInt() != MAGIC
                || input.readUnsignedByte() != VERSION) {
                throw invalid("User Spot relocation envelope prefix is invalid");
            }
            String stableType = text(input);
            String spotId = text(input);
            long generation = positive(
                input.readLong(), "Spot object generation");
            boolean restoreSnapshot = input.readBoolean();
            byte[] state = component(input);
            byte[] timers = component(input);
            int actorCount = boundedCount(
                input.readInt(), MAX_PARTICIPANTS, "Actor participant");
            List<ZLinkUserSpotAggregateStagingOwner.ActorParticipant> actors =
                new ArrayList<>(actorCount);
            String previousActor = null;
            for (int index = 0; index < actorCount; index++) {
                String actorId = text(input);
                if (previousActor != null
                    && compareUtf8(previousActor, actorId) >= 0) {
                    throw invalid("Actor inventory is not canonical");
                }
                previousActor = actorId;
                String actorType = text(input);
                long actorGeneration = positive(
                    input.readLong(), "Actor object generation");
                boolean actorSnapshot = input.readBoolean();
                byte[] actorState = component(input);
                actors.add(new ZLinkUserSpotAggregateStagingOwner.ActorParticipant(
                    actorId,
                    actorType,
                    actorState,
                    actorSnapshot,
                    new ZLinkBackendActorRef(
                        targetNodeRid,
                        actorId,
                        actorGeneration)));
            }
            int laneCount = boundedCount(
                input.readInt(), MAX_LANES, "accepted journal lane");
            LinkedHashMap<String, List<ZLinkAsyncSerialQueue.QueuedRecord>>
                journal = new LinkedHashMap<>();
            String previousLane = null;
            int recordCount = 0;
            for (int index = 0; index < laneCount; index++) {
                String lane = text(input);
                if (previousLane != null
                    && compareUtf8(previousLane, lane) >= 0) {
                    throw invalid("accepted journal lanes are not canonical");
                }
                previousLane = lane;
                int count = boundedCount(
                    input.readInt(), MAX_RECORDS, "accepted journal record");
                recordCount = Math.addExact(recordCount, count);
                if (recordCount > MAX_RECORDS) {
                    throw invalid("accepted journal record count exceeds its bound");
                }
                List<ZLinkAsyncSerialQueue.QueuedRecord> records =
                    new ArrayList<>(count);
                long previousSequence = 0;
                for (int recordIndex = 0; recordIndex < count; recordIndex++) {
                    long sequence = positive(
                        input.readLong(), "accepted journal sequence");
                    if (sequence <= previousSequence) {
                        throw invalid(
                            "accepted journal sequence is not increasing");
                    }
                    previousSequence = sequence;
                    records.add(new ZLinkAsyncSerialQueue.QueuedRecord(
                        sequence,
                        component(input)));
                }
                journal.put(lane, List.copyOf(records));
            }
            if (input.available() != 0) {
                throw invalid(
                    "User Spot relocation envelope contains trailing bytes");
            }
            Class<? extends ZLinkSpot<?>> spotType = spotTypes.apply(stableType);
            if (spotType == null) {
                throw invalid(
                    "target does not register User Spot type: " + stableType);
            }
            return new ZLinkUserSpotAggregateStagingOwner.Request(
                spotType,
                stableType,
                spotId,
                generation,
                state,
                restoreSnapshot,
                timers,
                actors,
                journal);
        } catch (EOFException failure) {
            throw invalid("User Spot relocation envelope is truncated", failure);
        } catch (IOException | ArithmeticException failure) {
            throw invalid("User Spot relocation envelope is invalid", failure);
        }
    }

    private static void component(DataOutputStream output, byte[] value)
        throws IOException {
        byte[] bytes = Objects.requireNonNull(value, "value");
        if (bytes.length > MAX_COMPONENT_BYTES) {
            throw invalid("relocation component exceeds its bound");
        }
        output.writeInt(bytes.length);
        output.write(bytes);
    }

    private static byte[] component(DataInputStream input) throws IOException {
        int length = input.readInt();
        if (length < 0 || length > MAX_COMPONENT_BYTES) {
            throw invalid("relocation component length is invalid");
        }
        byte[] bytes = input.readNBytes(length);
        if (bytes.length != length) {
            throw new EOFException();
        }
        return bytes;
    }

    private static void text(DataOutputStream output, String value)
        throws IOException {
        byte[] bytes = requireText(value).getBytes(StandardCharsets.UTF_8);
        output.writeShort(bytes.length);
        output.write(bytes);
    }

    private static String text(DataInputStream input) throws IOException {
        int length = input.readUnsignedShort();
        if (length < 1 || length > MAX_TEXT_BYTES) {
            throw invalid("relocation text length is invalid");
        }
        byte[] bytes = input.readNBytes(length);
        if (bytes.length != length) {
            throw new EOFException();
        }
        try {
            return StandardCharsets.UTF_8.newDecoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(ByteBuffer.wrap(bytes))
                .toString();
        } catch (CharacterCodingException failure) {
            throw invalid("relocation text is not strict UTF-8", failure);
        }
    }

    private static String requireText(String value) {
        if (value == null || value.isBlank()) {
            throw invalid("relocation text is required");
        }
        if (value.getBytes(StandardCharsets.UTF_8).length > MAX_TEXT_BYTES) {
            throw invalid("relocation text exceeds its UTF-8 bound");
        }
        return value;
    }

    private static int boundedCount(int count, int max, String name) {
        if (count < 0 || count > max) {
            throw invalid(name + " count exceeds its bound");
        }
        return count;
    }

    private static int compareUtf8(String left, String right) {
        return java.util.Arrays.compareUnsigned(
            left.getBytes(StandardCharsets.UTF_8),
            right.getBytes(StandardCharsets.UTF_8));
    }

    private static long positive(long value, String name) {
        if (value <= 0) {
            throw invalid(name + " must be positive");
        }
        return value;
    }

    private static IllegalArgumentException invalid(String message) {
        return new IllegalArgumentException(message);
    }

    private static IllegalArgumentException invalid(
        String message,
        Throwable cause) {
        return new IllegalArgumentException(message, cause);
    }
}

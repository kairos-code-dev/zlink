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
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.time.Duration;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;

/**
 * Infrastructure-only request/reply bridge for User Spot relocation control.
 * The immutable payload remains in Relocation Store; commands carry only the
 * exact aggregate, node, owner and root fences needed by the target.
 */
final class ZLinkSpotRetireControl {
    private static final int MAGIC = 0x5a4c5243; // ZLRC
    private static final int VERSION = 1;
    private static final int STAGE = 1;
    private static final int PUBLISH = 2;
    private static final int ABORT = 3;
    private static final int ACK = 127;
    private static final int MAX_TEXT_BYTES = 4096;
    private static final int MAX_COMMAND_BYTES = 1024 * 1024;

    private ZLinkSpotRetireControl() {
    }

    static Client client(ZLinkInternalMeshNode node) {
        return new Client(node);
    }

    static Target install(
        ZLinkInternalMeshNode node,
        TargetEndpoint endpoint) {
        Target target = new Target(endpoint);
        node.setRelocationControlHandler(target::handle);
        return target;
    }

    static final class Client {
        private final ZLinkInternalMeshNode node;

        private Client(ZLinkInternalMeshNode node) {
            this.node = Objects.requireNonNull(node, "node");
        }

        CompletionStage<Void> stage(
            RoutingId targetNodeRid,
            StageRequest request,
            Duration timeout) {
            return invoke(
                targetNodeRid,
                request.fence(),
                encodeStage(request),
                timeout);
        }

        CompletionStage<Void> publish(
            RoutingId targetNodeRid,
            Fence fence,
            Duration timeout) {
            return invoke(
                targetNodeRid,
                fence,
                encodeFence(PUBLISH, fence),
                timeout);
        }

        CompletionStage<Void> abort(
            RoutingId targetNodeRid,
            Fence fence,
            Duration timeout) {
            return invoke(
                targetNodeRid,
                fence,
                encodeFence(ABORT, fence),
                timeout);
        }

        private CompletionStage<Void> invoke(
            RoutingId targetNodeRid,
            Fence expectedFence,
            byte[] command,
            Duration timeout) {
            return node.requestRelocationControl(
                    targetNodeRid,
                    command,
                    timeout)
                .thenApply(reply -> {
                    if (!decodeAck(reply).equals(expectedFence)) {
                        throw new IllegalArgumentException(
                            "relocation acknowledgment fence differs");
                    }
                    return null;
                });
        }
    }

    static final class Target {
        private final TargetEndpoint endpoint;
        private final ConcurrentHashMap<Fence, Slot> slots =
            new ConcurrentHashMap<>();

        private Target(TargetEndpoint endpoint) {
            this.endpoint = Objects.requireNonNull(endpoint, "endpoint");
        }

        CompletionStage<byte[]> handle(
            RoutingId transportSource,
            byte[] encoded) {
            Command command = decode(encoded);
            if (command instanceof StageCommand stage) {
                if (!stage.request().sourceNodeRid().equals(transportSource)) {
                    return failed(new IllegalArgumentException(
                        "relocation source RID does not match transport"));
                }
                return stage(stage, encoded);
            }
            Slot slot = slots.get(command.fence());
            if (slot == null
                || !slot.request.sourceNodeRid().equals(transportSource)) {
                return failed(new IllegalStateException(
                    "relocation target stage is unavailable"));
            }
            if (command instanceof PublishCommand) {
                return publish(slot);
            }
            return abort(slot);
        }

        private CompletionStage<byte[]> stage(
            StageCommand command,
            byte[] encoded) {
            byte[] digest = sha256(encoded);
            Slot candidate = new Slot(command.request(), digest);
            Slot slot = slots.putIfAbsent(command.fence(), candidate);
            if (slot == null) {
                slot = candidate;
                try {
                    endpoint.stage(command.request())
                        .whenComplete((ignored, failure) -> {
                            if (failure == null) {
                                candidate.staged.complete(null);
                            } else {
                                slots.remove(command.fence(), candidate);
                                candidate.staged.completeExceptionally(
                                    unwrap(failure));
                            }
                        });
                } catch (RuntimeException failure) {
                    slots.remove(command.fence(), candidate);
                    candidate.staged.completeExceptionally(failure);
                }
            } else if (!Arrays.equals(slot.stageDigest, digest)) {
                return failed(new IllegalArgumentException(
                    "duplicate relocation stage payload differs"));
            }
            synchronized (slot) {
                if (slot.aborted) {
                    return failed(new IllegalStateException(
                        "aborted relocation cannot be staged again"));
                }
            }
            return slot.staged.thenApply(ignored -> encodeAck(command.fence()));
        }

        private CompletionStage<byte[]> publish(Slot slot) {
            synchronized (slot) {
                if (slot.aborted) {
                    return failed(new IllegalStateException(
                        "aborted relocation cannot be published"));
                }
                if (slot.published != null) {
                    return slot.published;
                }
                slot.published = slot.staged
                    .thenCompose(ignored -> endpoint.publish(slot.request))
                    .thenApply(ignored -> encodeAck(slot.request.fence()));
                return slot.published;
            }
        }

        private CompletionStage<byte[]> abort(Slot slot) {
            synchronized (slot) {
                if (slot.published != null) {
                    return failed(new IllegalStateException(
                        "published relocation cannot roll back to source"));
                }
                if (slot.aborted) {
                    return CompletableFuture.completedFuture(
                        encodeAck(slot.request.fence()));
                }
                slot.aborted = true;
            }
            return slot.staged.handle((ignored, stageFailure) -> null)
                .thenCompose(ignored -> endpoint.abort(slot.request))
                .thenApply(ignored -> encodeAck(slot.request.fence()));
        }
    }

    interface TargetEndpoint {
        CompletionStage<Void> stage(StageRequest request);

        CompletionStage<Void> publish(StageRequest request);

        CompletionStage<Void> abort(StageRequest request);
    }

    record Fence(UUID aggregateId, long aggregateGeneration) {
        Fence {
            Objects.requireNonNull(aggregateId, "aggregateId");
            if (aggregateGeneration <= 0) {
                throw new IllegalArgumentException(
                    "aggregate generation must be positive");
            }
        }
    }

    record StageRequest(
        Fence fence,
        RoutingId sourceNodeRid,
        long sourceNodeGeneration,
        String sourceOwnerId,
        long sourceOwnerLeaseGeneration,
        RoutingId targetNodeRid,
        long targetNodeGeneration,
        String targetOwnerId,
        long targetOwnerLeaseGeneration,
        String meshName,
        String spotId,
        String stableType,
        boolean instanceSpot,
        String relocationReference,
        long relocationChecksum) {
        StageRequest {
            Objects.requireNonNull(fence, "fence");
            Objects.requireNonNull(sourceNodeRid, "sourceNodeRid");
            Objects.requireNonNull(targetNodeRid, "targetNodeRid");
            requireText(sourceOwnerId, "sourceOwnerId");
            requireText(targetOwnerId, "targetOwnerId");
            requireText(meshName, "meshName");
            requireText(spotId, "spotId");
            requireText(stableType, "stableType");
            requireText(relocationReference, "relocationReference");
            if (sourceNodeGeneration <= 0
                || sourceOwnerLeaseGeneration <= 0
                || targetNodeGeneration <= 0
                || targetOwnerLeaseGeneration <= 0
                || relocationChecksum < 0
                || relocationChecksum > 0xffff_ffffL) {
                throw new IllegalArgumentException(
                    "relocation stage contains an invalid generation or checksum");
            }
        }
    }

    private sealed interface Command permits
        StageCommand, PublishCommand, AbortCommand {
        Fence fence();
    }

    private record StageCommand(StageRequest request) implements Command {
        @Override public Fence fence() { return request.fence(); }
    }

    private record PublishCommand(Fence fence) implements Command {
    }

    private record AbortCommand(Fence fence) implements Command {
    }

    private static final class Slot {
        private final StageRequest request;
        private final byte[] stageDigest;
        private final CompletableFuture<Void> staged = new CompletableFuture<>();
        private CompletionStage<byte[]> published;
        private boolean aborted;

        private Slot(StageRequest request, byte[] stageDigest) {
            this.request = request;
            this.stageDigest = stageDigest;
        }
    }

    private static byte[] encodeStage(StageRequest request) {
        return write(STAGE, output -> {
            writeFence(output, request.fence());
            writeRid(output, request.sourceNodeRid());
            output.writeLong(request.sourceNodeGeneration());
            writeText(output, request.sourceOwnerId());
            output.writeLong(request.sourceOwnerLeaseGeneration());
            writeRid(output, request.targetNodeRid());
            output.writeLong(request.targetNodeGeneration());
            writeText(output, request.targetOwnerId());
            output.writeLong(request.targetOwnerLeaseGeneration());
            writeText(output, request.meshName());
            writeText(output, request.spotId());
            writeText(output, request.stableType());
            output.writeBoolean(request.instanceSpot());
            writeText(output, request.relocationReference());
            output.writeInt((int) request.relocationChecksum());
        });
    }

    private static byte[] encodeFence(int kind, Fence fence) {
        return write(kind, output -> writeFence(output, fence));
    }

    private static byte[] encodeAck(Fence fence) {
        return write(ACK, output -> writeFence(output, fence));
    }

    private static Command decode(byte[] encoded) {
        byte[] bytes = Objects.requireNonNull(encoded, "encoded");
        if (bytes.length == 0 || bytes.length > MAX_COMMAND_BYTES) {
            throw new IllegalArgumentException(
                "relocation command exceeds its size bound");
        }
        try {
            DataInputStream input = new DataInputStream(
                new ByteArrayInputStream(bytes));
            if (input.readInt() != MAGIC || input.readUnsignedByte() != VERSION) {
                throw new IllegalArgumentException(
                    "relocation command prefix is invalid");
            }
            int kind = input.readUnsignedByte();
            Command command;
            if (kind == STAGE) {
                Fence fence = readFence(input);
                command = new StageCommand(new StageRequest(
                    fence,
                    readRid(input),
                    positive(input.readLong(), "sourceNodeGeneration"),
                    readText(input),
                    positive(input.readLong(), "sourceOwnerLeaseGeneration"),
                    readRid(input),
                    positive(input.readLong(), "targetNodeGeneration"),
                    readText(input),
                    positive(input.readLong(), "targetOwnerLeaseGeneration"),
                    readText(input),
                    readText(input),
                    readText(input),
                    input.readBoolean(),
                    readText(input),
                    Integer.toUnsignedLong(input.readInt())));
            } else if (kind == PUBLISH) {
                command = new PublishCommand(readFence(input));
            } else if (kind == ABORT) {
                command = new AbortCommand(readFence(input));
            } else {
                throw new IllegalArgumentException(
                    "relocation command kind is invalid");
            }
            if (input.available() != 0) {
                throw new IllegalArgumentException(
                    "relocation command contains trailing bytes");
            }
            return command;
        } catch (EOFException failure) {
            throw new IllegalArgumentException(
                "relocation command is truncated", failure);
        } catch (IOException failure) {
            throw new IllegalArgumentException(
                "relocation command is invalid", failure);
        }
    }

    private static Fence decodeAck(byte[] encoded) {
        try {
            DataInputStream input = new DataInputStream(
                new ByteArrayInputStream(encoded));
            if (input.readInt() != MAGIC
                || input.readUnsignedByte() != VERSION
                || input.readUnsignedByte() != ACK) {
                throw new IllegalArgumentException(
                    "relocation command acknowledgment is invalid");
            }
            Fence fence = readFence(input);
            if (input.available() != 0) {
                throw new IllegalArgumentException(
                    "relocation acknowledgment contains trailing bytes");
            }
            return fence;
        } catch (IOException failure) {
            throw new IllegalArgumentException(
                "relocation acknowledgment is invalid", failure);
        }
    }

    private static byte[] write(int kind, IoWriter writer) {
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DataOutputStream output = new DataOutputStream(bytes);
            output.writeInt(MAGIC);
            output.writeByte(VERSION);
            output.writeByte(kind);
            writer.write(output);
            output.flush();
            byte[] result = bytes.toByteArray();
            if (result.length > MAX_COMMAND_BYTES) {
                throw new IllegalArgumentException(
                    "relocation command exceeds its size bound");
            }
            return result;
        } catch (IOException failure) {
            throw new IllegalStateException(
                "relocation command could not be encoded", failure);
        }
    }

    private static void writeFence(DataOutputStream output, Fence fence)
        throws IOException {
        output.writeLong(fence.aggregateId().getMostSignificantBits());
        output.writeLong(fence.aggregateId().getLeastSignificantBits());
        output.writeLong(fence.aggregateGeneration());
    }

    private static Fence readFence(DataInputStream input) throws IOException {
        return new Fence(
            new UUID(input.readLong(), input.readLong()),
            positive(input.readLong(), "aggregateGeneration"));
    }

    private static void writeRid(DataOutputStream output, RoutingId rid)
        throws IOException {
        byte[] bytes = rid.toBytes();
        output.writeShort(bytes.length);
        output.write(bytes);
    }

    private static RoutingId readRid(DataInputStream input)
        throws IOException {
        int length = input.readUnsignedShort();
        if (length < 1 || length > 255) {
            throw new IllegalArgumentException(
                "relocation RID length is invalid");
        }
        byte[] bytes = input.readNBytes(length);
        if (bytes.length != length) {
            throw new EOFException();
        }
        return RoutingId.from(bytes);
    }

    private static void writeText(DataOutputStream output, String value)
        throws IOException {
        byte[] bytes = requireText(value, "value")
            .getBytes(StandardCharsets.UTF_8);
        output.writeShort(bytes.length);
        output.write(bytes);
    }

    private static String readText(DataInputStream input) throws IOException {
        int length = input.readUnsignedShort();
        if (length < 1 || length > MAX_TEXT_BYTES) {
            throw new IllegalArgumentException(
                "relocation text length is invalid");
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
            throw new IllegalArgumentException(
                "relocation text is not strict UTF-8", failure);
        }
    }

    private static String requireText(String value, String name) {
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException(name + " is required");
        }
        int length = value.getBytes(StandardCharsets.UTF_8).length;
        if (length > MAX_TEXT_BYTES) {
            throw new IllegalArgumentException(
                name + " exceeds its UTF-8 bound");
        }
        return value;
    }

    private static long positive(long value, String name) {
        if (value <= 0) {
            throw new IllegalArgumentException(name + " must be positive");
        }
        return value;
    }

    private static byte[] sha256(byte[] value) {
        try {
            return MessageDigest.getInstance("SHA-256").digest(value);
        } catch (NoSuchAlgorithmException failure) {
            throw new IllegalStateException("SHA-256 is unavailable", failure);
        }
    }

    private static Throwable unwrap(Throwable failure) {
        Throwable current = failure;
        while (current instanceof CompletionException
            && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
    }

    private static <T> CompletionStage<T> failed(Throwable failure) {
        return CompletableFuture.failedFuture(failure);
    }

    @FunctionalInterface
    private interface IoWriter {
        void write(DataOutputStream output) throws IOException;
    }
}

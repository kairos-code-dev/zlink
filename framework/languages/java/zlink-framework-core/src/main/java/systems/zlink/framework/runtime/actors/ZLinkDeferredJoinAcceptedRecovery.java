package systems.zlink.framework.runtime.actors;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.time.Duration;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.BiFunction;
import java.util.function.Function;
import java.util.function.Supplier;
import systems.zlink.framework.ZLinkEncodedPayload;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorJoinCompletion;
import systems.zlink.framework.actors.ZLinkActorJoinOperationId;
import systems.zlink.framework.locations.ZLinkRelocationFound;
import systems.zlink.framework.locations.ZLinkRelocationStore;
import systems.zlink.framework.locations.ZLinkStoreCancellation;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;

/**
 * Stores and replays the cross-node Accepted completion as part of the
 * relocation operation rather than relying on the source process callback.
 */
final class ZLinkDeferredJoinAcceptedRecovery {
    private static final int FORMAT_VERSION = 1;
    private static final int CURSOR_COMMITTED = 1;
    private static final Duration RETENTION = Duration.ofHours(24);
    private static final ZLinkStoreCancellation NOT_CANCELLED = () -> false;

    private final ZLinkRelocationStore store;
    private final ZLinkMessageSerializer serializer;
    private final Set<ZLinkActorJoinOperationId> delivered =
        ConcurrentHashMap.newKeySet();

    ZLinkDeferredJoinAcceptedRecovery(
        ZLinkRelocationStore store,
        ZLinkMessageSerializer serializer) {
        this.store = Objects.requireNonNull(store, "store");
        this.serializer = Objects.requireNonNull(serializer, "serializer");
    }

    CompletionStage<Manifest> prepare(
        ZLinkActorJoinOperationId operationId,
        ZLinkBackendActorRef actor,
        byte[] rawReply) {
        byte[] payload = encode(operationId, actor, rawReply);
        return store.put(payload, RETENTION, NOT_CANCELLED)
            .thenApply(stored -> new Manifest(
                stored.reference(),
                stored.checksumCrc32c(),
                CURSOR_COMMITTED));
    }

    CompletionStage<Void> deliver(
        Manifest manifest,
        ZLinkBackendActorRef currentActor,
        ZLinkActorRuntime runtime) {
        return deliver(
            manifest,
            currentActor,
            runtime::actorById,
            runtime::submitActorDispatch);
    }

    CompletionStage<Void> deliver(
        Manifest manifest,
        ZLinkBackendActorRef currentActor,
        Function<String, ZLinkActor> actorResolver,
        BiFunction<String, Supplier<CompletionStage<Void>>, CompletionStage<Void>>
            mailbox) {
        return store.get(manifest.reference(), NOT_CANCELLED)
            .thenCompose(result -> {
                if (!(result instanceof ZLinkRelocationFound found)) {
                    return CompletableFuture.failedFuture(
                        new IllegalStateException(
                            "deferred Actor Join completion root is missing"));
                }
                if (crc32c(found.payload()) != manifest.checksumCrc32c()) {
                    return CompletableFuture.failedFuture(
                        new IllegalStateException(
                            "deferred Actor Join completion checksum mismatch"));
                }
                Stored stored = decode(found.payload());
                if (!stored.actorId().equals(currentActor.actorId())
                    || stored.objectGeneration() != currentActor.generation()) {
                    return CompletableFuture.failedFuture(
                        new IllegalStateException(
                            "deferred Actor Join completion generation fence is stale"));
                }
                if (!delivered.add(stored.operationId())) {
                    return CompletableFuture.completedFuture(null);
                }
                ZLinkActor actor = actorResolver.apply(stored.actorId());
                if (actor == null) {
                    delivered.remove(stored.operationId());
                    return CompletableFuture.failedFuture(
                        new IllegalStateException(
                            "target Actor is not materialized for Join completion"));
                }
                ActorRef publicRef = ZLinkActorRuntime.toPublicActorRef(currentActor);
                ZLinkMessage reply = stored.rawReply().length == 0
                    ? ZLinkMessage.empty()
                    : ZLinkMessage.fromEncoded(
                        ZLinkEncodedPayload.from(stored.rawReply()),
                        serializer);
                return mailbox.apply(
                        stored.actorId(),
                        () -> {
                            CompletionStage<Void> callback =
                                actor.onJoinCompleted(
                                    new ZLinkActorJoinCompletion.Accepted(
                                        stored.operationId(),
                                        publicRef,
                                        reply));
                            return callback == null
                                ? CompletableFuture.completedFuture(null)
                                : callback;
                        })
                    .whenComplete((ignored, error) -> {
                        if (error != null) {
                            // A retry re-enters the Actor mailbox with the same
                            // OperationId. Applications use it as the side-effect
                            // idempotency key.
                            delivered.remove(stored.operationId());
                        }
                    });
            });
    }

    private static byte[] encode(
        ZLinkActorJoinOperationId operationId,
        ZLinkBackendActorRef actor,
        byte[] rawReply) {
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            try (DataOutputStream output = new DataOutputStream(bytes)) {
                output.writeInt(FORMAT_VERSION);
                output.writeLong(operationId.high());
                output.writeLong(operationId.low());
                output.writeUTF(actor.actorId());
                output.writeLong(actor.generation());
                byte[] reply = rawReply == null ? new byte[0] : rawReply.clone();
                output.writeInt(reply.length);
                output.write(reply);
            }
            return bytes.toByteArray();
        } catch (IOException impossible) {
            throw new IllegalStateException(impossible);
        }
    }

    private static Stored decode(byte[] payload) {
        try (DataInputStream input =
                 new DataInputStream(new ByteArrayInputStream(payload))) {
            if (input.readInt() != FORMAT_VERSION) {
                throw new IllegalStateException(
                    "unsupported deferred Actor Join completion format");
            }
            ZLinkActorJoinOperationId operationId =
                new ZLinkActorJoinOperationId(input.readLong(), input.readLong());
            String actorId = input.readUTF();
            long objectGeneration = input.readLong();
            int replyLength = input.readInt();
            if (replyLength < 0 || replyLength > payload.length) {
                throw new IllegalStateException(
                    "invalid deferred Actor Join completion reply length");
            }
            byte[] reply = input.readNBytes(replyLength);
            if (reply.length != replyLength || input.available() != 0) {
                throw new IllegalStateException(
                    "truncated deferred Actor Join completion root");
            }
            return new Stored(operationId, actorId, objectGeneration, reply);
        } catch (IOException error) {
            throw new IllegalStateException(
                "invalid deferred Actor Join completion root",
                error);
        }
    }

    private static long crc32c(byte[] payload) {
        java.util.zip.CRC32C checksum = new java.util.zip.CRC32C();
        checksum.update(payload, 0, payload.length);
        return checksum.getValue();
    }

    record Manifest(
        String reference,
        long checksumCrc32c,
        int cursor) {
        Manifest {
            Objects.requireNonNull(reference, "reference");
            if (reference.isBlank() || cursor != CURSOR_COMMITTED) {
                throw new IllegalArgumentException(
                    "invalid deferred Actor Join completion manifest");
            }
        }
    }

    private record Stored(
        ZLinkActorJoinOperationId operationId,
        String actorId,
        long objectGeneration,
        byte[] rawReply) {
    }
}

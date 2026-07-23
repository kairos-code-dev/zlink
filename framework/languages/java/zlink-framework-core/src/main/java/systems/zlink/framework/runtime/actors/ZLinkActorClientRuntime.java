package systems.zlink.framework.runtime.actors;

import java.time.Duration;
import java.util.EnumSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.actors.ZLinkActorRequestCall;
import systems.zlink.framework.actors.ZLinkActorSendCall;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdmissionKey;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolvers;
import systems.zlink.framework.runtime.messaging.ZLinkMessagePayloads;
import systems.zlink.framework.runtime.messaging.ZLinkPacketNames;
import systems.zlink.framework.runtime.messaging.ZLinkSubmitResults;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodec;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.runtime.streams.ZLinkStreamFrameCodec;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

public final class ZLinkActorClientRuntime implements ZLinkActorClient {
    private static final Duration FALLBACK_ROUTE_RETRY_TIMEOUT = Duration.ofSeconds(5);

    private final java.util.function.Supplier<ZLinkInternalSpotNode> spotNode;
    private final ZLinkStoreLocationResolvers locations;
    private final ZLinkMessageSerializer serializer;
    private final Duration defaultTimeout;

    public ZLinkActorClientRuntime(
        java.util.function.Supplier<ZLinkInternalSpotNode> spotNode,
        ZLinkStoreLocationResolvers locations,
        ZLinkMessageSerializer serializer,
        Duration defaultTimeout) {
        this.spotNode = java.util.Objects.requireNonNull(spotNode, "spotNode");
        this.locations = java.util.Objects.requireNonNull(locations, "locations");
        this.serializer = java.util.Objects.requireNonNull(serializer, "serializer");
        this.defaultTimeout = defaultTimeout == null ? Duration.ZERO : defaultTimeout;
    }

    @Override
    public ZLinkActorSendCall sendToActor(ActorRef actorRef, Object message) {
        return new SendCall(actorRef, message);
    }

    @Override
    public ZLinkActorRequestCall requestToActor(ActorRef actorRef, Object request) {
        return new RequestCall(actorRef, request);
    }

    private <TReply> CompletionStage<TReply> requestAsync(
        String actorId,
        String packetName,
        Object request,
        Duration timeout,
        Class<TReply> replyType) {
        return resolveActorAddress(actorId)
            .thenCompose(actor -> submitRequestWithRouteRetry(actor, packetName, request, timeout, replyType)
                .exceptionallyCompose(error -> {
                    if (isStaleActorError(error)) {
                        return reResolveActorAddress(actorId)
                            .thenCompose(reResolved -> submitRequestWithRouteRetry(
                                reResolved,
                                packetName,
                                request,
                                timeout,
                                replyType))
                            .exceptionallyCompose(retryError -> {
                                if (isStaleActorError(retryError)) {
                                    return failed(actorLocationStale(actorId, retryError));
                                }
                                return failed(unwrap(retryError));
                            });
                    }
                    return failed(unwrap(error));
                }));
    }

    private <TReply> CompletionStage<TReply> requestAsync(
        ActorRef actorRef,
        String packetName,
        Object request,
        Duration timeout,
        Class<TReply> replyType) {
        ZLinkBackendActorRef actor = toBackendActorRef(actorRef);
        return submitRequestWithRouteRetry(
                actor,
                packetName,
                request,
                timeout,
                replyType)
            .exceptionallyCompose(error -> classifyExplicitRefFailure(actorRef, error)
                .thenCompose(ZLinkActorClientRuntime::failed));
    }

    private CompletionStage<RuntimeException> classifyExplicitRefFailure(
        ActorRef requested,
        Throwable error) {
        Throwable unwrapped = unwrap(error);
        RuntimeException original = unwrapped instanceof RuntimeException runtime
            ? runtime
            : new ZLinkConfigurationException("Actor request failed.", unwrapped);
        if (!isStaleActorError(unwrapped)) {
            return CompletableFuture.completedFuture(original);
        }
        return locations.resolveActorRow(new ZLinkActorLocationKey(requested.actorId()))
            .handle((row, lookupError) -> {
                if (lookupError != null || row == null || row.actorRef() == null) {
                    return original;
                }
                ZLinkBackendActorRef current = toBackendActorRef(row);
                if (!current.nodeRid().equals(requested.nodeRid())
                    || current.generation() != requested.generation()) {
                    return actorLocationStale(requested.actorId(), original);
                }
                return original;
            });
    }

    private <TReply> CompletionStage<TReply> submitRequestWithRouteRetry(
        ZLinkBackendActorRef actor,
        String packetName,
        Object request,
        Duration timeout,
        Class<TReply> replyType) {
        Duration effectiveTimeout = timeout == null ? defaultTimeout : timeout;
        return ZLinkActorRetryScheduler.retryRouteUntil(
                routeRetryTimeout(effectiveTimeout),
                () -> submitRequest(actor, packetName, request, timeout, replyType),
                ZLinkActorClientRuntime::isRouteNotConnected)
            .exceptionallyCompose(error -> failed(unwrap(error)));
    }

    private CompletionStage<ZLinkBackendActorRef> resolveActorAddress(String actorId) {
        return locations.resolveActorRow(new ZLinkActorLocationKey(actorId))
            .thenApply(row -> {
                if (row == null || row.actorRef() == null) {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ACTOR_ROUTE_NOT_FOUND,
                        "Actor route '" + actorId + "' was not found.");
                }
                return rememberAuthority(row);
            });
    }

    private CompletionStage<ZLinkBackendActorRef> reResolveActorAddress(String actorId) {
        return locations.resolveActorRow(new ZLinkActorLocationKey(actorId))
            .thenApply(row -> {
                if (row == null || row.actorRef() == null) {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ACTOR_LOCATION_STALE,
                        "Actor route '" + actorId + "' became stale.");
                }
                return rememberAuthority(row);
            });
    }

    private ZLinkBackendActorRef rememberAuthority(
        ZLinkActorLocation row) {
        ZLinkBackendActorRef actor = toBackendActorRef(row);
        spotNode.get().rememberActorAuthority(actor, row.generation());
        return actor;
    }

    private CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult>
    submitSendResult(
        ActorRef actorRef,
        String packetName,
        Object message) {
        ZLinkBackendActorRef actor = toBackendActorRef(actorRef);
        List<Message> parts = createPacketParts(
            ZLinkStreamMessageKind.SEND,
            Optional.empty(),
            packetName,
            message);
        ZLinkInternalSpotNode node = spotNode.get();
        return ZLinkSubmitResults.submitAsync(
                node,
                ZLinkBackendAdmissionKey.actor(
                    actor.nodeRid(), actor.actorId(), actor.generation()),
                () -> node.sendToActor(
                    actor, parts, SendFlags.DONT_WAIT),
                () -> closeAll(parts))
            .exceptionallyCompose(error -> classifyExplicitRefFailure(actorRef, error)
                .thenCompose(classified -> isStaleActorError(classified)
                    ? CompletableFuture.completedFuture(ZLinkSubmitResults.result(
                        systems.zlink.framework.channels.ZLinkSubmitStatus.TARGET_NOT_FOUND))
                    : ZLinkActorClientRuntime.failed(classified)));
    }

    private <TReply> CompletionStage<TReply> submitRequest(
        ZLinkBackendActorRef actor,
        String packetName,
        Object request,
        Duration timeout,
        Class<TReply> replyType) {
        List<Message> parts = createPacketParts(
            ZLinkStreamMessageKind.REQUEST,
            Optional.of(1L),
            packetName,
            request);
        CompletionStage<List<Message>> replyStage;
        try {
            replyStage = spotNode.get().requestToActor(
                actor,
                parts,
                SendFlags.NONE,
                timeout == null ? defaultTimeout : timeout);
        } catch (RuntimeException error) {
            closeAll(parts);
            return failed(mapBackendException(error, "Actor request"));
        }
        closeAll(parts);
        return replyStage.handle((replyParts, error) -> {
            if (error != null) {
                throw new CompletionException(mapBackendException(unwrap(error), "Actor request"));
            }
            try {
                return decodeReply(replyParts, replyType);
            } finally {
                closeAll(replyParts);
            }
        });
    }

    private List<Message> createPacketParts(
        ZLinkStreamMessageKind kind,
        Optional<Long> requestSeq,
        String packetName,
        Object message) {
        ZLinkStreamHeader header = new ZLinkStreamHeader(
            kind,
            ZLinkStreamCodec.JSON,
            EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
            requestSeq,
            packetName,
            Map.of());
        Message headerPart = Message.from(ZLinkStreamHeaderCodec.encode(header));
        Message payloadPart = ZLinkMessagePayloads.message(
            systems.zlink.framework.messaging.ZLinkMessage.of(message),
            serializer);
        return List.of(headerPart, payloadPart);
    }

    private <TReply> TReply decodeReply(List<Message> reply, Class<TReply> replyType) {
        if (reply == null || reply.isEmpty()) {
            throw new ZLinkConfigurationException("Actor request reply is empty.");
        }
        if (reply.size() == 1) {
            byte[] frame = reply.get(0).toByteArray();
            Optional<ZLinkStreamFrameCodec.DecodedFrame> decoded = ZLinkStreamFrameCodec.tryDecode(frame);
            if (decoded.isPresent()) {
                ZLinkStreamFrameCodec.DecodedFrame decodedFrame = decoded.get();
                Message payload = Message.from(decodedFrame.body());
                try {
                    return decodePayload(
                        ZLinkStreamHeaderCodec.decodeOrPlain(decodedFrame.header()),
                        payload,
                        replyType);
                } finally {
                    payload.close();
                }
            }
        }
        if (reply.size() < 2) {
            throw new ZLinkConfigurationException("Actor request reply payload is missing.");
        }
        ZLinkStreamHeader header = ZLinkStreamHeaderCodec.decodeOrPlain(reply.get(0).toByteArray());
        return decodePayload(header, reply.get(1), replyType);
    }

    private <TReply> TReply decodePayload(
        ZLinkStreamHeader header,
        Message payload,
        Class<TReply> replyType) {
        if (header.kind() == ZLinkStreamMessageKind.ERROR) {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.REQUEST_FAILED,
                payload.toUtf8String());
        }
        return ZLinkMessagePayloads.deserialize(serializer, payload, replyType);
    }

    private RuntimeException mapBackendException(Throwable error, String operationName) {
        Throwable unwrapped = unwrap(error);
        if (unwrapped instanceof ZlinkRequestException request) {
            return switch (request.getResult()) {
                case NOT_CONNECTED -> new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ROUTE_NOT_CONNECTED,
                    operationName + " failed because the target route is not connected.",
                    true,
                    request);
                case NOT_FOUND -> new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ACTOR_ROUTE_NOT_FOUND,
                    operationName + " failed because the actor route was not found.",
                    request);
                case CONFLICT -> new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ACTOR_LOCATION_STALE,
                    operationName + " failed because the actor location is stale.",
                    true,
                    request);
                default -> request;
            };
        }
        if (unwrapped instanceof ZlinkSubmitException submit) {
            return switch (submit.getResult()) {
                case NOT_CONNECTED -> new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ROUTE_NOT_CONNECTED,
                    operationName + " failed because the target route is not connected.",
                    true,
                    submit);
                case NOT_FOUND -> new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ACTOR_ROUTE_NOT_FOUND,
                    operationName + " failed because the actor route was not found.",
                    submit);
                default -> submit;
            };
        }
        String text = unwrapped.getMessage() == null ? "" : unwrapped.getMessage();
        if (text.contains("NOT_CONNECTED")) {
            return new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ROUTE_NOT_CONNECTED,
                operationName + " failed because the target route is not connected.",
                true,
                unwrapped);
        }
        if (text.contains("NOT_FOUND")) {
            return new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ACTOR_ROUTE_NOT_FOUND,
                operationName + " failed because the actor route was not found.",
                unwrapped);
        }
        if (text.contains("CONFLICT")) {
            return new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ACTOR_LOCATION_STALE,
                operationName + " failed because the actor location is stale.",
                true,
                unwrapped);
        }
        if (unwrapped instanceof RuntimeException runtimeException) {
            return runtimeException;
        }
        return new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.REQUEST_FAILED,
            operationName + " failed.",
            unwrapped);
    }

    private static ZLinkBackendActorRef toBackendActorRef(ZLinkActorLocation row) {
        return new ZLinkBackendActorRef(
            row.actorRef().nodeRid(),
            row.actorRef().actorId(),
            row.actorRef().generation());
    }

    private static ZLinkBackendActorRef toBackendActorRef(ActorRef actorRef) {
        java.util.Objects.requireNonNull(actorRef, "actorRef");
        return new ZLinkBackendActorRef(
            actorRef.nodeRid(),
            actorRef.actorId(),
            actorRef.generation());
    }

    private static boolean isStaleActorError(Throwable error) {
        Throwable unwrapped = unwrap(error);
        return unwrapped instanceof ZLinkFrameworkException frameworkError
            && (frameworkError.kind() == ZLinkFrameworkErrorKind.ACTOR_ROUTE_NOT_FOUND
                || frameworkError.kind() == ZLinkFrameworkErrorKind.ACTOR_LOCATION_STALE);
    }

    private static boolean isRouteNotConnected(Throwable error) {
        Throwable unwrapped = unwrap(error);
        return unwrapped instanceof ZLinkFrameworkException frameworkError
            && frameworkError.kind() == ZLinkFrameworkErrorKind.ROUTE_NOT_CONNECTED;
    }

    private static Duration routeRetryTimeout(Duration timeout) {
        return timeout == null || timeout.isZero() || timeout.isNegative()
            ? FALLBACK_ROUTE_RETRY_TIMEOUT
            : timeout;
    }

    private static RuntimeException actorLocationStale(String actorId, Throwable error) {
        return new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ACTOR_LOCATION_STALE,
            "Actor route '" + actorId + "' is stale after re-resolve.",
            true,
            unwrap(error));
    }

    private static Throwable unwrap(Throwable error) {
        Throwable current = error;
        while (current instanceof CompletionException && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
    }

    private static <T> CompletableFuture<T> failed(Throwable error) {
        CompletableFuture<T> future = new CompletableFuture<>();
        future.completeExceptionally(error);
        return future;
    }

    private static void closeAll(List<Message> parts) {
        if (parts == null) {
            return;
        }
        for (Message part : parts) {
            part.close();
        }
    }

    private final class SendCall implements ZLinkActorSendCall {
        private final systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate submitGate =
            new systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate();
        private final ActorRef actorRef;
        private final Object message;
        private String packetName;

        SendCall(ActorRef actorRef, Object message) {
            this.actorRef = java.util.Objects.requireNonNull(actorRef, "actorRef");
            this.message = message;
            this.packetName = ZLinkPacketNames.resolve(message);
        }

        public ZLinkActorSendCall packetName(String packetName) {
            this.packetName = packetName;
            return this;
        }

        @Override
        public CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> submit() {
            CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> duplicate =
                submitGate.begin();
            if (duplicate != null) {
                return duplicate;
            }
            return submitSendResult(actorRef, packetName, message);
        }
    }

    private final class RequestCall implements ZLinkActorRequestCall {
        private final ActorRef actorRef;
        private final Object request;
        private String packetName;
        private Duration timeout;

        RequestCall(ActorRef actorRef, Object request) {
            this.actorRef = java.util.Objects.requireNonNull(actorRef, "actorRef");
            this.request = request;
            this.packetName = ZLinkPacketNames.resolve(request);
        }

        public ZLinkActorRequestCall packetName(String packetName) {
            this.packetName = packetName;
            return this;
        }

        @Override
        public ZLinkActorRequestCall timeout(Duration timeout) {
            this.timeout = timeout;
            return this;
        }

        @Override
        public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
            return systems.zlink.framework.execution.ZLinkAsyncSerialQueue.manageCurrent(
                requestAsync(actorRef, packetName, request, timeout, replyType));
        }
    }
}

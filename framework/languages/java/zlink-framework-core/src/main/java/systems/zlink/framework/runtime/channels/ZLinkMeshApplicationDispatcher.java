package systems.zlink.framework.runtime.channels;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.CompletionStage;
import java.util.function.Consumer;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.Dispatch;
import systems.zlink.contracts.service.spot.RecordKind;
import systems.zlink.contracts.service.spot.ReplyToken;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshDispatchRecord;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshApplicationReceiver;

import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.internal.drain.ZLinkMeshDrainCoordinator;
import systems.zlink.framework.runtime.mesh.MeshNodeRegistration;
import systems.zlink.framework.runtime.messaging.ZLinkFrameworkErrorReply;
import systems.zlink.framework.runtime.messaging.ZLinkApplicationMetadata;
import systems.zlink.framework.runtime.messaging.ZLinkPacketNames;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerScanner;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandler;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerCatalog;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerKind;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerSurface;

/**
 * Dispatches MeshNode application records through the framework's typed
 * serializer and handler activation path.
 */
public final class ZLinkMeshApplicationDispatcher
    implements ZLinkMeshApplicationReceiver {
    @FunctionalInterface
    interface ReplySender {
        void send(ReplyToken token, List<Message> parts);
    }

    private static final String NODE_NAMESPACE = "";

    private final ZLinkChannelHandlerInvoker invoker;
    private final ReplySender replies;
    private final String meshName;
    private final ZLinkMeshDrainCoordinator drains;
    private final Map<String, Namespace> namespaces = new HashMap<>();

    public ZLinkMeshApplicationDispatcher(
        MeshNodeRegistration mesh,
        ZLinkMessageSerializer serializer,
        ZLinkFrameworkRegistration framework,
        ZLinkHandlerActivator handlerFactory) {
        this(mesh, serializer, framework, handlerFactory,
            (token, parts) -> Dispatch.reply(token, parts, SendFlags.NONE), null);
    }

    public ZLinkMeshApplicationDispatcher(
        MeshNodeRegistration mesh,
        ZLinkMessageSerializer serializer,
        ZLinkFrameworkRegistration framework,
        ZLinkHandlerActivator handlerFactory,
        ZLinkMeshDrainCoordinator drains) {
        this(mesh, serializer, framework, handlerFactory,
            (token, parts) -> Dispatch.reply(token, parts, SendFlags.NONE), drains);
    }

    ZLinkMeshApplicationDispatcher(
        MeshNodeRegistration mesh,
        ZLinkMessageSerializer serializer,
        ZLinkFrameworkRegistration framework,
        ZLinkHandlerActivator handlerFactory,
        ReplySender replies) {
        this(mesh, serializer, framework, handlerFactory, replies, null);
    }

    ZLinkMeshApplicationDispatcher(
        MeshNodeRegistration mesh,
        ZLinkMessageSerializer serializer,
        ZLinkFrameworkRegistration framework,
        ZLinkHandlerActivator handlerFactory,
        ReplySender replies,
        ZLinkMeshDrainCoordinator drains) {
        Objects.requireNonNull(mesh, "mesh");
        Objects.requireNonNull(framework, "framework");
        this.meshName = mesh.meshName();
        this.drains = drains;
        this.replies = Objects.requireNonNull(replies, "replies");
        this.invoker = new ZLinkChannelHandlerInvoker(
            Objects.requireNonNull(serializer, "serializer"),
            framework.codecs(),
            Objects.requireNonNull(handlerFactory, "handlerFactory"),
            framework.handlerExecutor(),
            framework.suspendHandlerInvokers(),
            framework.filters());
        ZLinkScannedHandlerCatalog scannedHandlers =
            ZLinkHandlerScanner.scan(framework.handlerPackageMarkers());
        int localPendingCapacity =
            mesh.configureRouterSocket().receiveHighWaterMark() > 0
                ? mesh.configureRouterSocket().receiveHighWaterMark()
                : 4096;
        namespaces.put(
            NODE_NAMESPACE,
            routeNamespace(mesh.nodeHandlers(), localPendingCapacity));
        mesh.channelHandlers().forEach((name, handlers) ->
            namespaces.put(name, channelNamespace(
                name,
                handlers,
                mesh.channelHandlerGroups().getOrDefault(name, List.of()),
                scannedHandlers)));
    }

    @Override
    public void accept(ZLinkMeshDispatchRecord record) {
        Objects.requireNonNull(record, "record");
        ZLinkMeshDrainCoordinator.Claim claim = drains == null
            ? null
            : drains.tryClaim(meshName);
        if (drains != null && claim == null) {
            reject(record, "RouteMesh application admission is sealed", null);
            return;
        }
        RecordKind kind = record.receive().kind();
        Namespace namespace = namespace(kind, record.receive().channelName());
        if (namespace == null || record.parts().size() < 2) {
            reject(record, "MeshNode message has no registered handler namespace or payload", claim);
            return;
        }

        String packetName = record.parts().get(0).toUtf8String();
        Message payload = record.parts().get(1);
        Map<String, String> metadata;
        try {
            metadata = ZLinkApplicationMetadata.decode(
                record.receive().applicationMetadata());
        } catch (IllegalArgumentException error) {
            reject(record, error.getMessage(), claim);
            return;
        }
        switch (kind) {
            case NODE_SEND, CHANNEL_SEND ->
                dispatchSend(record, namespace, packetName, payload, metadata, claim);
            case NODE_REQUEST, CHANNEL_REQUEST ->
                dispatchRequest(record, namespace, packetName, payload, metadata, claim);
            default -> closeRecord(record, claim);
        }
    }

    @Override
    public void setLocalNodeReadyHandler(Runnable handler) {
        namespaces.get(NODE_NAMESPACE).sendQueue.onCapacityAvailable(handler);
    }

    @Override
    public int submitLocalNodeSend(
        systems.zlink.contracts.core.RoutingId sourceNodeRid,
        byte[] metadataBytes,
        List<Message> parts) {
        Namespace namespace = namespaces.get(NODE_NAMESPACE);
        if (namespace == null || parts.size() < 2) {
            return ZLinkOneWayCalls.TARGET_NOT_FOUND;
        }
        String packetName = parts.get(0).toUtf8String();
        ChannelRouteSendHandlerRegistration route = namespace.routeSends.get(packetName);
        if (route == null) {
            return ZLinkOneWayCalls.TARGET_NOT_FOUND;
        }
        ZLinkMeshDrainCoordinator.Claim claim = drains == null
            ? null
            : drains.tryClaim(meshName);
        if (drains != null && claim == null) {
            return ZLinkOneWayCalls.SHUTDOWN;
        }

        Message payload = null;
        try {
            payload = Message.from(parts.get(1));
            Map<String, String> metadata = ZLinkApplicationMetadata.decode(metadataBytes);
            Message ownedPayload = payload;
            boolean accepted = namespace.sendQueue.tryEnqueue(() -> {
                try {
                    return invoker.executeHandler(() -> invoker.invokeRouteSendHandler(
                            null, route, sourceNodeRid, ownedPayload, metadata))
                        .whenComplete((ignored, error) -> {
                            ownedPayload.close();
                            if (claim != null) {
                                claim.close();
                            }
                        });
                } catch (RuntimeException failure) {
                    ownedPayload.close();
                    if (claim != null) {
                        claim.close();
                    }
                    return java.util.concurrent.CompletableFuture.failedFuture(failure);
                }
            });
            if (!accepted) {
                payload.close();
                if (claim != null) {
                    claim.close();
                }
                return ZLinkOneWayCalls.BACKPRESSURED;
            }
            return ZLinkOneWayCalls.SUBMITTED;
        } catch (RuntimeException failure) {
            if (payload != null) {
                payload.close();
            }
            if (claim != null) {
                claim.close();
            }
            throw failure;
        }
    }

    private void dispatchSend(
        ZLinkMeshDispatchRecord record,
        Namespace namespace,
        String packetName,
        Message payload,
        Map<String, String> metadata,
        ZLinkMeshDrainCoordinator.Claim claim) {
        ChannelRouteSendHandlerRegistration route = namespace.routeSends.get(packetName);
        ChannelSendHandlerRegistration channel = namespace.channelSends.get(packetName);
        if (route == null && channel == null) {
            closeRecord(record, claim);
            return;
        }
        namespace.sendQueue.enqueue(() -> {
            CompletionStage<Void> invocation = route != null
                ? invoker.executeHandler(() -> invoker.invokeRouteSendHandler(
                    null,
                    route,
                    record.receive().sourceNodeRid(),
                    payload,
                    metadata))
                : invoker.executeHandler(() ->
                    invoker.invokeSendHandler(
                        record.receive().channelName(),
                        channel,
                        payload,
                        metadata));
            return invocation.whenComplete((ignored, error) -> closeRecord(record, claim));
        });
    }

    private void dispatchRequest(
        ZLinkMeshDispatchRecord record,
        Namespace namespace,
        String packetName,
        Message payload,
        Map<String, String> metadata,
        ZLinkMeshDrainCoordinator.Claim claim) {
        ReplyToken token = record.receive().replyToken();
        ChannelRouteRequestHandlerRegistration route = namespace.routeRequests.get(packetName);
        ChannelRequestHandlerRegistration channel = namespace.channelRequests.get(packetName);
        if ((token == null && !record.canReply())
            || (route == null && channel == null)) {
            reject(record, "MeshNode request handler is not registered: " + packetName, claim);
            return;
        }
        namespace.requestQueue.enqueue(() -> {
            CompletionStage<Message> invocation = route != null
                ? invoker.executeHandler(() -> invoker.invokeRouteRequestHandler(
                    null,
                    route,
                    record.receive().sourceNodeRid(),
                    payload,
                    metadata))
                : invoker.executeHandler(() ->
                    invoker.invokeRequestHandler(
                        record.receive().channelName(),
                        channel,
                        payload,
                        metadata));
            return invocation.<Void>handle((reply, error) -> {
                if (error == null) {
                    replyAndClose(record, token, List.of(reply));
                } else {
                    replyError(record, token, error.getMessage());
                }
                return null;
            }).whenComplete((ignored, error) -> closeRecord(record, claim));
        });
    }

    private void reject(ZLinkMeshDispatchRecord record, String message) {
        reject(record, message, null);
    }

    private void reject(
        ZLinkMeshDispatchRecord record,
        String message,
        ZLinkMeshDrainCoordinator.Claim claim) {
        ReplyToken token = record.receive().replyToken();
        try {
            if (token != null || record.canReply()) {
                replyError(record, token, message);
            }
        } finally {
            closeRecord(record, claim);
        }
    }

    private static void closeRecord(
        ZLinkMeshDispatchRecord record,
        ZLinkMeshDrainCoordinator.Claim claim) {
        try {
            record.close();
        } finally {
            if (claim != null) {
                claim.close();
            }
        }
    }

    private void replyError(ReplyToken token, String message) {
        replyAndClose(token, ZLinkFrameworkErrorReply.create(message));
    }

    private void replyError(
        ZLinkMeshDispatchRecord record,
        ReplyToken token,
        String message) {
        replyAndClose(record, token, ZLinkFrameworkErrorReply.create(message));
    }

    private void replyAndClose(ReplyToken token, List<Message> parts) {
        try {
            replies.send(token, parts);
        } finally {
            parts.forEach(Message::close);
        }
    }

    private void replyAndClose(
        ZLinkMeshDispatchRecord record,
        ReplyToken token,
        List<Message> parts) {
        try {
            if (record.canReply()) {
                record.reply(parts);
            } else {
                replies.send(token, parts);
            }
        } finally {
            parts.forEach(Message::close);
        }
    }

    private Namespace namespace(RecordKind kind, String channelName) {
        return switch (kind) {
            case NODE_SEND, NODE_REQUEST -> namespaces.get(NODE_NAMESPACE);
            case CHANNEL_SEND, CHANNEL_REQUEST -> namespaces.get(channelName);
            default -> null;
        };
    }

    private static Namespace routeNamespace(
        List<MeshNodeRegistration.DispatchHandler> handlers,
        int sendPendingCapacity) {
        Namespace namespace = new Namespace(sendPendingCapacity);
        for (MeshNodeRegistration.DispatchHandler handler : handlers) {
            String packetName = ZLinkPacketNames.resolve(handler.messageType());
            if (handler.request()) {
                putUnique(namespace.routeRequests, packetName,
                    new ChannelRouteRequestHandlerRegistration(
                        handler.handlerType(),
                        handler.messageType(),
                        handler.replyType(),
                        packetName));
            } else {
                putUnique(namespace.routeSends, packetName,
                    new ChannelRouteSendHandlerRegistration(
                        handler.handlerType(),
                        handler.messageType(),
                        packetName));
            }
        }
        return namespace;
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private static Namespace channelNamespace(
        String channelName,
        List<MeshNodeRegistration.DispatchHandler> handlers,
        List<String> handlerGroups,
        ZLinkScannedHandlerCatalog scannedHandlers) {
        Namespace namespace = new Namespace(Integer.MAX_VALUE);
        Set<String> groups = handlerGroups.isEmpty()
            ? Set.of(channelName)
            : Set.copyOf(handlerGroups);
        for (ZLinkScannedHandler handler : scannedHandlers.matching(
            groups,
            ZLinkScannedHandlerSurface.CHANNEL,
            ZLinkScannedHandlerKind.REQUEST)) {
            putUnique(namespace.channelRequests, handler.packetName(),
                new ChannelRequestHandlerRegistration(
                    handler.handlerType(),
                    handler.handlerMethod(),
                    handler.messageType(),
                    handler.replyType(),
                    handler.packetName()));
        }
        for (ZLinkScannedHandler handler : scannedHandlers.matching(
            groups,
            ZLinkScannedHandlerSurface.CHANNEL,
            ZLinkScannedHandlerKind.SEND)) {
            putUnique(namespace.channelSends, handler.packetName(),
                new ChannelSendHandlerRegistration(
                    handler.handlerType(),
                    handler.handlerMethod(),
                    handler.messageType(),
                    handler.packetName()));
        }
        for (MeshNodeRegistration.DispatchHandler handler : handlers) {
            String packetName = ZLinkPacketNames.resolve(handler.messageType());
            if (handler.request()) {
                putUnique(namespace.channelRequests, packetName,
                    new ChannelRequestHandlerRegistration(
                        handler.handlerType(),
                        handler.messageType(),
                        handler.replyType(),
                        packetName));
            } else {
                putUnique(namespace.channelSends, packetName,
                    new ChannelSendHandlerRegistration(
                        handler.handlerType(),
                        handler.messageType(),
                        packetName));
            }
        }
        return namespace;
    }

    private static <T> void putUnique(Map<String, T> handlers, String packetName, T handler) {
        if (handlers.putIfAbsent(packetName, handler) != null) {
            throw new ZLinkConfigurationException(
                "duplicate MeshNode handler packet name: " + packetName);
        }
    }

    private static final class Namespace {
        private final Map<String, ChannelRouteSendHandlerRegistration> routeSends =
            new HashMap<>();
        private final Map<String, ChannelRouteRequestHandlerRegistration> routeRequests =
            new HashMap<>();
        private final Map<String, ChannelSendHandlerRegistration> channelSends =
            new HashMap<>();
        private final Map<String, ChannelRequestHandlerRegistration> channelRequests =
            new HashMap<>();
        private final ZLinkAsyncSerialQueue sendQueue;
        private final ZLinkAsyncSerialQueue requestQueue = new ZLinkAsyncSerialQueue();

        Namespace(int sendPendingCapacity) {
            sendQueue = new ZLinkAsyncSerialQueue(false, sendPendingCapacity);
        }
    }
}

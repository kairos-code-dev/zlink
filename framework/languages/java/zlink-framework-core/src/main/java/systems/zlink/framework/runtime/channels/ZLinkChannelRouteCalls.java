package systems.zlink.framework.runtime.channels;

import systems.zlink.framework.runtime.backend.*;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.sockets.SubmitResult;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.locks.LockSupport;
import java.util.function.Supplier;
import java.util.logging.Logger;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkCloseException;
import systems.zlink.contracts.errors.ZlinkRecvException;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.framework.ZLinkHandlerContext;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkClientServerChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteSendContext;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSocketRuntimeOptions;
import systems.zlink.framework.configuration.ZLinkDispatchErrorAction;
import systems.zlink.framework.configuration.ZLinkDispatchErrorReason;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.configuration.ZLinkDispatchFailure;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.runtime.configuration.ZLinkCodecRegistration;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.diagnostics.ZLinkDispatchErrorReporter;
import systems.zlink.framework.runtime.handlers.ZLinkFilterPipeline;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerScanner;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerMethodInvoker;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerStages;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandler;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerCatalog;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerKind;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerSurface;
import systems.zlink.framework.runtime.internal.handlers.ZLinkSuspendInvocationAdapter;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.runtime.messaging.ZLinkMessagePayloads;
import systems.zlink.framework.runtime.messaging.ZLinkApplicationMetadata;
import systems.zlink.framework.runtime.messaging.ZLinkSubmitResults;

final class RouteSendCall implements ZLinkSendCall {
    private final systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate submitGate =
        new systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate();
    private final ZLinkChannelCallRuntime runtime;
    private final ZLinkBackendRouterSocket router;
    private final RoutingId target;
    private final Message payload;
    private final Optional<String> packetName;

    RouteSendCall(
        ZLinkChannelCallRuntime runtime,
        ZLinkBackendRouterSocket router,
        RoutingId target,
        Message payload,
        Optional<String> packetName) {
        this.runtime = runtime;
        this.router = router;
        this.target = target;
        this.payload = payload;
        this.packetName = packetName;
    }

    RouteSendCall(
        ZLinkChannelCallRuntime runtime,
        ZLinkBackendRouterSocket router,
        RoutingId target,
        Message payload) {
        this(runtime, router, target, payload, Optional.empty());
    }

    public ZLinkSendCall packetName(String packetName) {
        return new RouteSendCall(runtime, router, target, payload, Optional.of(packetName));
    }

    @Override
    public CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> submit() {
        CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> duplicate =
            submitGate.begin();
        if (duplicate != null) {
            return duplicate;
        }
        if (runtime.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
            runtime.flow().trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.SENT,
                ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                ZLinkDispatchMessageKind.SEND,
                packetName.orElse(null), null, null, null, target.toString(), null, null, null));
        }
        List<Message> sendParts = ZLinkChannelCallRuntime.parts(packetName, payload);
        return ZLinkSubmitResults.submitAsync(
            router,
            ZLinkBackendAdmissionKey.socket(),
            () -> router.send(target, sendParts, SendFlags.DONT_WAIT),
            () -> sendParts.forEach(Message::close));
    }
}

final class RouteRequestCall implements ZLinkRequestCall {
    private final ZLinkChannelCallRuntime runtime;
    private final String channelName;
    private final ZLinkBackendRouterSocket router;
    private final RoutingId target;
    private final Message payload;
    private final Optional<String> packetName;
    private final Duration timeout;

    RouteRequestCall(
        ZLinkChannelCallRuntime runtime,
        String channelName,
        ZLinkBackendRouterSocket router,
        RoutingId target,
        Message payload,
        Optional<String> packetName,
        Duration timeout) {
        this.runtime = runtime;
        this.channelName = channelName;
        this.router = router;
        this.target = target;
        this.payload = payload;
        this.packetName = packetName;
        this.timeout = timeout;
    }

    public ZLinkRequestCall packetName(String packetName) {
        return new RouteRequestCall(
            runtime,
            channelName,
            router,
            target,
            payload,
            Optional.of(packetName),
            timeout);
    }

    @Override
    public ZLinkRequestCall timeout(Duration timeout) {
        return new RouteRequestCall(runtime, channelName, router, target, payload, packetName, timeout);
    }

    @Override
    public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
        CompletableFuture<TReply> result = new CompletableFuture<>();
        runtime.track(result, timeout);
        List<Message> requestParts = ZLinkChannelCallRuntime.parts(packetName, payload);
        if (runtime.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
            runtime.flow().trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.SENT,
                ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                ZLinkDispatchMessageKind.REQUEST,
                packetName.orElse(null), channelName, null, null,
                target.toString(), null, null, null));
        }
        try {
            runtime.submitRoute(
                router,
                target,
                requestParts,
                reply -> {
                    try {
                        runtime.completeReply(reply, replyType, result);
                        if (runtime.flow().enabled(ZLinkMessageFlowOutcome.REPLY_RECEIVED)) {
                            runtime.flow().trace(new ZLinkMessageFlowEvent(
                                ZLinkMessageFlowOutcome.REPLY_RECEIVED,
                                ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                                ZLinkDispatchMessageKind.RESPONSE,
                                packetName.orElse(null), channelName, null, null,
                                target.toString(), null, null, null));
                        }
                    } catch (RuntimeException ex) {
                        result.completeExceptionally(ex);
                    } finally {
                        reply.parts().forEach(Message::close);
                    }
                },
                timeout,
                result);
        } finally {
            requestParts.forEach(Message::close);
        }
        return systems.zlink.framework.execution.ZLinkAsyncSerialQueue.manageCurrent(result);
    }

}

final class MeshNodeRouteSendCall implements ZLinkSendCall {
    private final systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate submitGate =
        new systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate();
    private final ZLinkChannelCallRuntime runtime;
    private final ZLinkInternalSpotNode node;
    private final RoutingId target;
    private final Message payload;
    private final Optional<String> packetName;
    private final ZLinkApplicationMetadata metadata;

    MeshNodeRouteSendCall(
        ZLinkChannelCallRuntime runtime,
        ZLinkInternalSpotNode node,
        RoutingId target,
        Message payload,
        Optional<String> packetName) {
        this(runtime, node, target, payload, packetName, ZLinkApplicationMetadata.empty());
    }

    private MeshNodeRouteSendCall(
        ZLinkChannelCallRuntime runtime,
        ZLinkInternalSpotNode node,
        RoutingId target,
        Message payload,
        Optional<String> packetName,
        ZLinkApplicationMetadata metadata) {
        this.runtime = runtime;
        this.node = node;
        this.target = target;
        this.payload = payload;
        this.packetName = packetName;
        this.metadata = metadata;
    }

    public ZLinkSendCall packetName(String name) {
        return new MeshNodeRouteSendCall(
            runtime, node, target, payload, Optional.of(name), metadata);
    }

    @Override
    public ZLinkSendCall metadata(String key, String value) {
        return new MeshNodeRouteSendCall(
            runtime, node, target, payload, packetName, metadata.with(key, value));
    }

    @Override
    public ZLinkSendCall metadata(Map<String, String> values) {
        return new MeshNodeRouteSendCall(
            runtime, node, target, payload, packetName, metadata.withAll(values));
    }

    @Override
    public CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> submit() {
        CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> duplicate =
            submitGate.begin();
        if (duplicate != null) {
            return duplicate;
        }
        if (runtime.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
            runtime.flow().trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.SENT,
                ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                ZLinkDispatchMessageKind.SEND,
                packetName.orElse(null), null, null, null, target.toString(), null, null, null));
        }
        List<Message> sendParts = ZLinkChannelCallRuntime.parts(packetName, payload);
        if (node.routingId().equals(target)) {
            return ZLinkSubmitResults.submitAsync(
                node,
                ZLinkBackendAdmissionKey.node(target),
                () -> submitLocal(node, target, metadata.encode(), sendParts),
                () -> sendParts.forEach(Message::close));
        }
        java.util.Optional<systems.zlink.framework.channels.ZLinkSubmitStatus> classified =
            node.classifyNodeSendTarget(target);
        if (classified.isPresent()) {
            sendParts.forEach(Message::close);
            return CompletableFuture.completedFuture(
                ZLinkSubmitResults.result(classified.orElseThrow()));
        }
        return ZLinkSubmitResults.submitAsync(
            node,
            ZLinkBackendAdmissionKey.node(target),
            () -> node.sendToNode(
                target, metadata.encode(), sendParts, SendFlags.DONT_WAIT),
            () -> sendParts.forEach(Message::close));
    }

    private static boolean submitLocal(
        ZLinkInternalSpotNode node,
        RoutingId target,
        byte[] metadata,
        List<Message> parts) {
        systems.zlink.framework.channels.ZLinkSubmitStatus status =
            node.submitLocalNodeSend(target, metadata, parts)
                .orElse(systems.zlink.framework.channels.ZLinkSubmitStatus.ROUTE_NOT_CONNECTED);
        return switch (status) {
            case SUBMITTED -> true;
            case BACKPRESSURED -> false;
            case TARGET_NOT_FOUND -> throw new ZlinkSubmitException(SubmitResult.NOT_FOUND);
            case ROUTE_NOT_CONNECTED ->
                throw new ZlinkSubmitException(SubmitResult.NOT_CONNECTED);
            case SHUTDOWN -> throw new ZlinkSubmitException(SubmitResult.TERMINATED);
            case TIMED_OUT -> throw new IllegalStateException(
                "local Node admission cannot return a timeout before waiting");
        };
    }
}

final class MeshChannelRouteSendCall implements ZLinkSendCall {
    private final systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate submitGate =
        new systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate();
    private final ZLinkChannelCallRuntime runtime;
    private final String channelName;
    private final ZLinkInternalSpotNode node;
    private final Message payload;
    private final Optional<String> packetName;
    private final ZLinkApplicationMetadata metadata;

    MeshChannelRouteSendCall(
        ZLinkChannelCallRuntime runtime,
        String channelName,
        ZLinkInternalSpotNode node,
        Message payload,
        Optional<String> packetName) {
        this(
            runtime,
            channelName,
            node,
            payload,
            packetName,
            ZLinkApplicationMetadata.empty());
    }

    private MeshChannelRouteSendCall(
        ZLinkChannelCallRuntime runtime,
        String channelName,
        ZLinkInternalSpotNode node,
        Message payload,
        Optional<String> packetName,
        ZLinkApplicationMetadata metadata) {
        this.runtime = runtime;
        this.channelName = channelName;
        this.node = node;
        this.payload = payload;
        this.packetName = packetName;
        this.metadata = metadata;
    }

    public ZLinkSendCall packetName(String name) {
        return new MeshChannelRouteSendCall(
            runtime, channelName, node, payload, Optional.of(name), metadata);
    }

    @Override
    public ZLinkSendCall metadata(String key, String value) {
        return new MeshChannelRouteSendCall(
            runtime, channelName, node, payload, packetName, metadata.with(key, value));
    }

    @Override
    public ZLinkSendCall metadata(Map<String, String> values) {
        return new MeshChannelRouteSendCall(
            runtime, channelName, node, payload, packetName, metadata.withAll(values));
    }

    @Override
    public CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> submit() {
        CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> duplicate =
            submitGate.begin();
        if (duplicate != null) {
            return duplicate;
        }
        List<Message> parts = ZLinkChannelCallRuntime.parts(packetName, payload);
        return ZLinkSubmitResults.submitAsync(
            node,
            ZLinkBackendAdmissionKey.channel(channelName),
            () -> node.sendToChannel(
                channelName, metadata.encode(), parts, SendFlags.DONT_WAIT),
            () -> parts.forEach(Message::close));
    }
}

final class MeshChannelRouteRequestCall implements ZLinkRequestCall {
    private final ZLinkChannelCallRuntime runtime;
    private final String channelName;
    private final ZLinkInternalSpotNode node;
    private final Message payload;
    private final Optional<String> packetName;
    private final Duration timeout;
    private final ZLinkApplicationMetadata metadata;

    MeshChannelRouteRequestCall(
        ZLinkChannelCallRuntime runtime,
        String channelName,
        ZLinkInternalSpotNode node,
        Message payload,
        Optional<String> packetName,
        Duration timeout) {
        this(
            runtime,
            channelName,
            node,
            payload,
            packetName,
            timeout,
            ZLinkApplicationMetadata.empty());
    }

    private MeshChannelRouteRequestCall(
        ZLinkChannelCallRuntime runtime,
        String channelName,
        ZLinkInternalSpotNode node,
        Message payload,
        Optional<String> packetName,
        Duration timeout,
        ZLinkApplicationMetadata metadata) {
        this.runtime = runtime;
        this.channelName = channelName;
        this.node = node;
        this.payload = payload;
        this.packetName = packetName;
        this.timeout = timeout;
        this.metadata = metadata;
    }

    public ZLinkRequestCall packetName(String name) {
        return new MeshChannelRouteRequestCall(
            runtime, channelName, node, payload, Optional.of(name), timeout, metadata);
    }

    @Override
    public ZLinkRequestCall metadata(String key, String value) {
        return new MeshChannelRouteRequestCall(
            runtime, channelName, node, payload, packetName, timeout,
            metadata.with(key, value));
    }

    @Override
    public ZLinkRequestCall metadata(Map<String, String> values) {
        return new MeshChannelRouteRequestCall(
            runtime, channelName, node, payload, packetName, timeout,
            metadata.withAll(values));
    }

    @Override
    public ZLinkRequestCall timeout(Duration value) {
        return new MeshChannelRouteRequestCall(
            runtime, channelName, node, payload, packetName, value, metadata);
    }

    @Override
    public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
        CompletableFuture<TReply> result = new CompletableFuture<>();
        runtime.track(result, timeout);
        long deadline = System.nanoTime() + timeout.toNanos();
        byte[] payloadBytes;
        try {
            payloadBytes = payload.toByteArray();
        } finally {
            payload.close();
        }
        submitAttempt(replyType, result, deadline, payloadBytes);
        return systems.zlink.framework.execution.ZLinkAsyncSerialQueue.manageCurrent(result);
    }

    private <TReply> void submitAttempt(
        Class<TReply> replyType,
        CompletableFuture<TReply> result,
        long deadline,
        byte[] payloadBytes) {
        if (result.isDone()) {
            return;
        }
        List<Message> parts = ZLinkChannelCallRuntime.parts(
            packetName,
            Message.from(payloadBytes));
        try {
            boolean submitted = node.requestToChannel(
                channelName,
                metadata.encode(),
                parts,
                reply -> {
                    try {
                        runtime.completeReply(reply, replyType, result);
                    } catch (RuntimeException error) {
                        result.completeExceptionally(error);
                    } finally {
                        reply.parts().forEach(Message::close);
                    }
                },
                SendFlags.NONE,
                timeout);
            if (!submitted) {
                if (System.nanoTime() < deadline) {
                    runtime.retryRouteRequest(() -> submitAttempt(
                        replyType, result, deadline, payloadBytes));
                } else {
                    result.completeExceptionally(new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.REQUEST_FAILED,
                        "RouteMesh channel request was not submitted to " + channelName));
                }
            }
        } catch (RuntimeException error) {
            if (ZLinkChannelRequestSubmitter.isRetriableSubmit(error)
                && System.nanoTime() < deadline) {
                runtime.retryRouteRequest(() -> submitAttempt(
                    replyType, result, deadline, payloadBytes));
            } else {
                result.completeExceptionally(error);
            }
        } finally {
            parts.forEach(Message::close);
        }
    }
}

final class MeshNodeRouteRequestCall implements ZLinkRequestCall {
    private final ZLinkChannelCallRuntime runtime;
    private final String channelName;
    private final ZLinkInternalSpotNode node;
    private final RoutingId target;
    private final Message payload;
    private final Optional<String> packetName;
    private final Duration timeout;
    private final ZLinkApplicationMetadata metadata;

    MeshNodeRouteRequestCall(
        ZLinkChannelCallRuntime runtime,
        String channelName,
        ZLinkInternalSpotNode node,
        RoutingId target,
        Message payload,
        Optional<String> packetName,
        Duration timeout) {
        this(
            runtime,
            channelName,
            node,
            target,
            payload,
            packetName,
            timeout,
            ZLinkApplicationMetadata.empty());
    }

    private MeshNodeRouteRequestCall(
        ZLinkChannelCallRuntime runtime,
        String channelName,
        ZLinkInternalSpotNode node,
        RoutingId target,
        Message payload,
        Optional<String> packetName,
        Duration timeout,
        ZLinkApplicationMetadata metadata) {
        this.runtime = runtime;
        this.channelName = channelName;
        this.node = node;
        this.target = target;
        this.payload = payload;
        this.packetName = packetName;
        this.timeout = timeout;
        this.metadata = metadata;
    }

    public ZLinkRequestCall packetName(String name) {
        return new MeshNodeRouteRequestCall(
            runtime, channelName, node, target, payload, Optional.of(name), timeout, metadata);
    }

    @Override
    public ZLinkRequestCall metadata(String key, String value) {
        return new MeshNodeRouteRequestCall(
            runtime, channelName, node, target, payload, packetName, timeout,
            metadata.with(key, value));
    }

    @Override
    public ZLinkRequestCall metadata(Map<String, String> values) {
        return new MeshNodeRouteRequestCall(
            runtime, channelName, node, target, payload, packetName, timeout,
            metadata.withAll(values));
    }

    @Override
    public ZLinkRequestCall timeout(Duration value) {
        return new MeshNodeRouteRequestCall(
            runtime, channelName, node, target, payload, packetName, value, metadata);
    }

    @Override
    public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
        CompletableFuture<TReply> result = new CompletableFuture<>();
        runtime.track(result, timeout);
        List<Message> requestParts = ZLinkChannelCallRuntime.parts(packetName, payload);
        if (runtime.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
            runtime.flow().trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.SENT,
                ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                ZLinkDispatchMessageKind.REQUEST,
                packetName.orElse(null), channelName, null, null,
                target.toString(), null, null, null));
        }
        try {
            boolean submitted = node.requestToNode(
                target,
                metadata.encode(),
                requestParts,
                reply -> {
                    try {
                        runtime.completeReply(reply, replyType, result);
                        if (runtime.flow().enabled(ZLinkMessageFlowOutcome.REPLY_RECEIVED)) {
                            runtime.flow().trace(new ZLinkMessageFlowEvent(
                                ZLinkMessageFlowOutcome.REPLY_RECEIVED,
                                ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                                ZLinkDispatchMessageKind.RESPONSE,
                                packetName.orElse(null), channelName, null, null,
                                target.toString(), null, null, null));
                        }
                    } catch (RuntimeException error) {
                        result.completeExceptionally(error);
                    } finally {
                        reply.parts().forEach(Message::close);
                    }
                },
                SendFlags.NONE,
                timeout);
            if (!submitted) {
                result.completeExceptionally(new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.REQUEST_FAILED,
                    "RouteMesh node request was not submitted to " + target));
            }
        } catch (RuntimeException error) {
            result.completeExceptionally(error);
        } finally {
            requestParts.forEach(Message::close);
        }
        return systems.zlink.framework.execution.ZLinkAsyncSerialQueue.manageCurrent(result);
    }
}

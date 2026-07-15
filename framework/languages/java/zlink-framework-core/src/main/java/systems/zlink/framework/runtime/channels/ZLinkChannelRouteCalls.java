package systems.zlink.framework.runtime.channels;

import systems.zlink.framework.runtime.backend.*;

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

final class RouteSendCall implements ZLinkSendCall {
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
    public void submit() {
        if (runtime.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
            runtime.flow().trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.SENT,
                ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                ZLinkDispatchMessageKind.SEND,
                packetName.orElse(null), null, null, null, target.toString(), null, null, null));
        }
        runtime.submitOneWay(() -> {
            List<Message> sendParts = ZLinkChannelCallRuntime.parts(packetName, payload);
            try {
                router.send(target, sendParts, SendFlags.NONE);
            } finally {
                sendParts.forEach(Message::close);
            }
        });
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

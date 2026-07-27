package systems.zlink.framework.runtime.channels;

import systems.zlink.framework.runtime.internal.backend.*;

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
import systems.zlink.framework.ZLinkMessageContext;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkClientServerChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkPublishMessageContext;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkRouteMessageContext;
import systems.zlink.framework.channels.ZLinkSendCall;
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
import systems.zlink.framework.runtime.internal.locations.ZLinkAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddress;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddressResolver;
import systems.zlink.framework.runtime.internal.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.runtime.internal.configuration.ZLinkCodecRegistration;
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


final class RouteSpotSendCall
    implements systems.zlink.framework.spots.ZLinkSpotSendCall {
    private final java.util.concurrent.atomic.AtomicBoolean submitGate =
        new java.util.concurrent.atomic.AtomicBoolean();
    private final ZLinkChannelCallRuntime runtime;
    private final String channelName;
    private final SpotTransportAddressResolver resolver;
    private final String target;
    private final Message payload;
    private final Optional<String> packetName;

    RouteSpotSendCall(
        ZLinkChannelCallRuntime runtime,
        String channelName,
        SpotTransportAddressResolver resolver,
        String target,
        Message payload,
        Optional<String> packetName) {
        this.runtime = runtime;
        this.channelName = channelName;
        this.resolver = resolver;
        this.target = target;
        this.payload = payload;
        this.packetName = packetName;
    }

    public ZLinkSendCall packetName(String packetName) {
        return new RouteSpotSendCall(
            runtime,
            channelName,
            resolver,
            target,
            payload,
            Optional.of(packetName));
    }

    @Override
    public CompletionStage<Void> submit() {
        CompletionStage<Void> duplicate =
            ZLinkOneWayCalls.beginOneWay(submitGate);
        if (duplicate != null) {
            return duplicate;
        }
        return ZLinkOneWayCalls.adaptOneWay(
            SpotCallAddresses.resolve(resolver, target).thenCompose(address -> {
            List<Message> sendParts = ZLinkChannelCallRuntime.parts(packetName, payload);
            try {
                return runtime.sendToSpot(
                    address.routerChannelId(),
                    address.targetNodeRid(),
                    address.spotId(),
                    address.spotGeneration(),
                    sendParts).whenComplete((ignored, error) -> sendParts.forEach(Message::close));
            } catch (RuntimeException error) {
                sendParts.forEach(Message::close);
                throw error;
            }
        }));
    }
}

final class RouteSpotRequestCall
    implements systems.zlink.framework.spots.ZLinkSpotRequestCall {
    private final ZLinkChannelCallRuntime runtime;
    private final String channelName;
    private final SpotTransportAddressResolver resolver;
    private final String target;
    private final Message payload;
    private final Optional<String> packetName;
    private final Duration timeout;

    RouteSpotRequestCall(
        ZLinkChannelCallRuntime runtime,
        String channelName,
        SpotTransportAddressResolver resolver,
        String target,
        Message payload,
        Optional<String> packetName,
        Duration timeout) {
        this.runtime = runtime;
        this.channelName = channelName;
        this.resolver = resolver;
        this.target = target;
        this.payload = payload;
        this.packetName = packetName;
        this.timeout = timeout;
    }

    public ZLinkRequestCall packetName(String packetName) {
        return new RouteSpotRequestCall(
            runtime,
            channelName,
            resolver,
            target,
            payload,
            Optional.of(packetName),
            timeout);
    }

    @Override
    public systems.zlink.framework.spots.ZLinkSpotRequestCall timeout(
        Duration timeout) {
        return new RouteSpotRequestCall(
            runtime,
            channelName,
            resolver,
            target,
            payload,
            packetName,
            timeout);
    }

    @Override
    public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
        systems.zlink.framework.runtime.internal.handlers
            .ZLinkSuspendInvocationContext.rejectSameSpotWait(target);
        CompletionStage<TReply> stage = SpotCallAddresses.resolve(resolver, target).thenCompose(address -> {
            List<Message> requestParts = ZLinkChannelCallRuntime.parts(packetName, payload);
            return runtime.requestToSpot(
                address.routerChannelId(),
                address.targetNodeRid(),
                address.spotId(),
                address.spotGeneration(),
                requestParts,
                timeout)
                .thenApply(replyParts -> {
                    try {
                        return runtime.decodeSpotReply(replyParts, replyType);
                    } finally {
                        replyParts.forEach(Message::close);
                    }
                }).whenComplete((ignored, error) -> requestParts.forEach(Message::close));
        });
        return systems.zlink.framework.execution.ZLinkAsyncSerialQueue.manageCurrent(stage);
    }

    @Override
    public <TReply> CompletionStage<TReply> yield(Class<TReply> replyType) {
        systems.zlink.framework.runtime.internal.handlers
            .ZLinkSuspendInvocationContext.requireYieldAllowed("Spot request");
        return systems.zlink.framework.execution.ZLinkAsyncSerialQueue
            .yieldCurrent(submit(replyType));
    }

}

final class SpotCallAddresses {
    private SpotCallAddresses() {
    }

    static CompletionStage<SpotTransportAddress> resolve(
        SpotTransportAddressResolver resolver,
        String target) {
        if (resolver == null) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "SpotHandle resolver is not configured"));
        }
        return resolver.resolve(target).thenCompose(address -> address
            .map(CompletableFuture::completedFuture)
            .orElseGet(() -> CompletableFuture.failedFuture(
                new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SPOT_ROUTE_NOT_FOUND,
                    "SpotHandle route is stale or unavailable"))));
    }
}

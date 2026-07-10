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
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkAwait;
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
import systems.zlink.framework.channels.ZLinkYieldRequestCall;
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
import systems.zlink.framework.execution.ZLinkFrameworkTurns;
import systems.zlink.framework.execution.ZLinkYieldTurn;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.runtime.configuration.ZLinkCodecRegistration;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.diagnostics.ZLinkDispatchErrorReporter;
import systems.zlink.framework.runtime.handlers.ZLinkFilterPipeline;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerScanner;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerMethodInvoker;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerStages;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandler;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerCatalog;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerKind;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerSurface;
import systems.zlink.framework.runtime.handlers.ZLinkSuspendHandlerInvoker;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.runtime.messaging.ZLinkMessagePayloads;

final class RouteSpotSendCall implements ZLinkSendCall {
    private final ZLinkChannelCallRuntime runtime;
    private final String channelName;
    private final RoutingId targetNode;
    private final RoutingId targetSpot;
    private final Message payload;
    private final Optional<String> packetName;

    RouteSpotSendCall(
        ZLinkChannelCallRuntime runtime,
        String channelName,
        RoutingId targetNode,
        RoutingId targetSpot,
        Message payload,
        Optional<String> packetName) {
        this.runtime = runtime;
        this.channelName = channelName;
        this.targetNode = targetNode;
        this.targetSpot = targetSpot;
        this.payload = payload;
        this.packetName = packetName;
    }

    @Override
    public ZLinkSendCall packetName(String packetName) {
        return new RouteSpotSendCall(
            runtime,
            channelName,
            targetNode,
            targetSpot,
            payload,
            Optional.of(packetName));
    }

    @Override
    public ZLinkSendCall metadata(String key, String value) {
        return this;
    }

    @Override
    public systems.zlink.framework.ZLinkSubmitStage submit() {
        List<Message> sendParts = ZLinkChannelCallRuntime.parts(packetName, payload);
        try {
            return systems.zlink.framework.ZLinkSubmitStage.from(
                runtime.sendToSpot(
                channelName,
                targetNode,
                targetSpot,
                sendParts));
        } finally {
            sendParts.forEach(Message::close);
        }
    }
}

final class RouteSpotRequestCall implements ZLinkRequestCall {
    private final ZLinkChannelCallRuntime runtime;
    private final String channelName;
    private final RoutingId targetNode;
    private final RoutingId targetSpot;
    private final Message payload;
    private final Optional<String> packetName;
    private final Duration timeout;
    private final ZLinkYieldTurn turn;

    RouteSpotRequestCall(
        ZLinkChannelCallRuntime runtime,
        String channelName,
        RoutingId targetNode,
        RoutingId targetSpot,
        Message payload,
        Optional<String> packetName,
        Duration timeout) {
        this(runtime, channelName, targetNode, targetSpot, payload, packetName, timeout,
            ZLinkFrameworkTurns.captureCurrent());
    }

    private RouteSpotRequestCall(
        ZLinkChannelCallRuntime runtime,
        String channelName,
        RoutingId targetNode,
        RoutingId targetSpot,
        Message payload,
        Optional<String> packetName,
        Duration timeout,
        ZLinkYieldTurn turn) {
        this.runtime = runtime;
        this.channelName = channelName;
        this.targetNode = targetNode;
        this.targetSpot = targetSpot;
        this.payload = payload;
        this.packetName = packetName;
        this.timeout = timeout;
        this.turn = turn;
    }

    @Override
    public ZLinkRequestCall packetName(String packetName) {
        return new RouteSpotRequestCall(
            runtime,
            channelName,
            targetNode,
            targetSpot,
            payload,
            Optional.of(packetName),
            timeout,
            turn);
    }

    @Override
    public ZLinkRequestCall metadata(String key, String value) {
        return this;
    }

    @Override
    public ZLinkRequestCall timeout(Duration timeout) {
        return new RouteSpotRequestCall(
            runtime,
            channelName,
            targetNode,
            targetSpot,
            payload,
            packetName,
            timeout,
            turn);
    }

    @Override
    public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
        List<Message> requestParts = ZLinkChannelCallRuntime.parts(packetName, payload);
        try {
            return runtime.requestToSpot(
                channelName,
                targetNode,
                targetSpot,
                requestParts,
                timeout)
                .thenApply(replyParts -> {
                    try {
                        return runtime.decodeSpotReply(replyParts, replyType);
                    } finally {
                        replyParts.forEach(Message::close);
                    }
                });
        } finally {
            requestParts.forEach(Message::close);
        }
    }

    @Override
    public <TReply> TReply await(Class<TReply> replyType) {
        CompletionStage<TReply> stage = submit(replyType);
        if (turn == null) {
            return ZLinkAwait.await(stage);
        }
        return ZLinkAwait.await(ZLinkFrameworkTurns.awaitManagedCompletion(turn, stage));
    }

}

package systems.zlink.framework.runtime.channels;

import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.runtime.backend.ZLinkBackendDealerSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendReceived;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestCallback;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendRouterSocket;
import systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer;

final class ZLinkChannelCallRuntime {
    @FunctionalInterface
    interface SpotSend {
        CompletionStage<Void> send(
            String channelName,
            RoutingId targetNode,
            RoutingId targetSpot,
            List<Message> parts);
    }

    @FunctionalInterface
    interface SpotRequest {
        CompletionStage<List<Message>> request(
            String channelName,
            RoutingId targetNode,
            RoutingId targetSpot,
            List<Message> parts,
            Duration timeout);
    }

    private final ZLinkMessageFlowTracer flow;
    private final ScheduledExecutorService timeoutExecutor;
    private final ZLinkChannelRequestSubmitter requestSubmitter;
    private final ZLinkChannelReplyDecoder replyDecoder;
    private final SpotSend spotSend;
    private final SpotRequest spotRequest;
    private final Set<CompletableFuture<?>> pendingRequests = ConcurrentHashMap.newKeySet();

    ZLinkChannelCallRuntime(
        ZLinkMessageFlowTracer flow,
        ScheduledExecutorService timeoutExecutor,
        Duration defaultTimeout,
        ZLinkChannelReplyDecoder replyDecoder,
        SpotSend spotSend,
        SpotRequest spotRequest) {
        this.flow = flow;
        this.timeoutExecutor = timeoutExecutor;
        this.requestSubmitter = new ZLinkChannelRequestSubmitter(
            timeoutExecutor,
            defaultTimeout);
        this.replyDecoder = replyDecoder;
        this.spotSend = spotSend;
        this.spotRequest = spotRequest;
    }

    ZLinkMessageFlowTracer flow() {
        return flow;
    }

    void track(CompletableFuture<?> result, Duration timeout) {
        pendingRequests.add(result);
        var timeoutTask = timeoutExecutor.schedule(
            () -> result.completeExceptionally(
                new TimeoutException("request timed out after " + timeout)),
            timeout.toNanos(),
            TimeUnit.NANOSECONDS);
        result.whenComplete((ignored, error) -> {
            timeoutTask.cancel(false);
            pendingRequests.remove(result);
        });
    }

    void submitClient(
        ZLinkBackendDealerSocket client,
        List<Message> requestParts,
        Duration timeout,
        ZLinkBackendRequestCallback callback,
        CompletableFuture<?> result) {
        requestSubmitter.submitClient(client, requestParts, timeout, callback, result);
    }

    void submitRoute(
        ZLinkBackendRouterSocket router,
        RoutingId target,
        List<Message> requestParts,
        ZLinkBackendRequestCallback callback,
        Duration timeout,
        CompletableFuture<?> result) {
        requestSubmitter.submitRoute(
            router,
            target,
            requestParts,
            callback,
            timeout,
            result);
    }

    <TReply> void completeReply(
        ZLinkBackendReceived reply,
        Class<TReply> replyType,
        CompletableFuture<TReply> result) {
        if (reply.result() != ZLinkBackendRequestResult.OK) {
            result.completeExceptionally(new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.REQUEST_FAILED,
                "channel request failed: " + reply.result()));
            return;
        }
        if (ZLinkChannelRuntime.isFrameworkErrorReply(reply.parts())) {
            result.completeExceptionally(new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.REQUEST_FAILED,
                ZLinkChannelRuntime.frameworkErrorReplyMessage(reply.parts())));
            return;
        }
        result.complete(replyDecoder.decode(
            reply.parts(),
            replyType,
            "route mesh reply decode failed"));
    }

    <TReply> TReply decodeSpotReply(List<Message> replies, Class<TReply> replyType) {
        return replyDecoder.decode(
            replies,
            replyType,
            "route mesh SPOT reply decode failed");
    }

    CompletionStage<Void> sendToSpot(
        String channelName,
        RoutingId targetNode,
        RoutingId targetSpot,
        List<Message> parts) {
        return spotSend.send(channelName, targetNode, targetSpot, parts);
    }

    CompletionStage<List<Message>> requestToSpot(
        String channelName,
        RoutingId targetNode,
        RoutingId targetSpot,
        List<Message> parts,
        Duration timeout) {
        return spotRequest.request(
            channelName,
            targetNode,
            targetSpot,
            parts,
            timeout);
    }

    void beginClose() {
        for (CompletableFuture<?> pending : pendingRequests) {
            pending.completeExceptionally(new ZLinkConfigurationException(
                "channel runtime is closed"));
        }
    }

    static List<Message> parts(Optional<String> packetName, Message payload) {
        if (packetName.isEmpty()) {
            return List.of(payload);
        }
        return List.of(
            Message.from(packetName.get().getBytes(StandardCharsets.UTF_8)),
            payload);
    }
}

package systems.zlink.framework.runtime.spots;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.SynchronousQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import systems.zlink.contracts.errors.ZlinkCloseException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkLogicalMulticastDetail;
import systems.zlink.framework.channels.ZLinkPublishResult;
import systems.zlink.framework.channels.ZLinkSubmitStatus;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.runtime.messaging.ZLinkApplicationMetadata;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.runtime.internal.metrics.ZLinkRuntimeMetrics;

final class ZLinkSpotPublisherRuntime implements AutoCloseable {
    private final ZLinkMessageSerializer serializer;
    private final ZLinkSpotRouteMessages messages;
    private final ZLinkMessageFlowTracer flow;
    private final ThreadPoolExecutor multicastExecutor;
    private final Map<String, ZLinkInternalSpotNode> nodesByChannel = new HashMap<>();
    private final Map<String, ZLinkBackendSpot> spotsByChannel = new HashMap<>();
    private volatile boolean closed;

    ZLinkSpotPublisherRuntime(
        ZLinkMessageSerializer serializer,
        ZLinkSpotRouteMessages messages,
        ZLinkMessageFlowTracer flow) {
        this.serializer = serializer;
        this.messages = messages;
        this.flow = flow;
        this.multicastExecutor = new ThreadPoolExecutor(
            0,
            Math.max(2, Runtime.getRuntime().availableProcessors()),
            30L,
            TimeUnit.SECONDS,
            new SynchronousQueue<>(),
            runnable -> {
                Thread thread = new Thread(runnable, "zlink-logical-multicast");
                thread.setDaemon(true);
                return thread;
            },
            new ThreadPoolExecutor.AbortPolicy());
    }

    void register(String channelName, ZLinkInternalSpotNode node) {
        nodesByChannel.put(channelName, node);
    }

    boolean contains(String channelName) {
        return nodesByChannel.containsKey(channelName);
    }

    ZLinkSpotPublisherClient client() {
        return new ZLinkDefaultSpotPublisherClient(this);
    }

    ZLinkPublishCall publish(
        String channelName,
        String topic,
        Object message) {
        return publish(channelName, channelName, topic, message);
    }

    ZLinkPublishCall publish(
        String meshName,
        String channelName,
        String topic,
        Object message) {
        if (channelName == null || channelName.isBlank()) {
            throw new ZLinkConfigurationException("SPOT publisher channel name is required");
        }
        if (topic == null || topic.isBlank()) {
            throw new ZLinkConfigurationException("SPOT publish topic is required");
        }
        requireChannel(meshName);
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return call(
            meshName,
            channelName,
            topic,
            encoded.payload(),
            Optional.of(encoded.packetName()));
    }

    ZLinkPublishCall call(
        String meshName,
        String channelName,
        String topic,
        Message payload,
        Optional<String> packetName) {
        requireChannel(meshName);
        return new ZLinkExternalSpotPublishCall(
            this, meshName, channelName, topic, payload, packetName);
    }

    ZLinkPublishResult submitNow(
        String meshName,
        String channelName,
        String topic,
        Message payload,
        Optional<String> packetName,
        ZLinkApplicationMetadata metadata) {
        trace(channelName, topic, packetName);
        List<Message> parts = messages.encode(packetName, payload);
        try {
            var detail = requireChannel(meshName).publishDetailed(
                channelName,
                topic,
                metadata.encode(),
                parts,
                SendFlags.DONT_WAIT);
            return new ZLinkPublishResult(
                detail.droppedRemoteTargetCount() > 0
                    ? ZLinkSubmitStatus.BACKPRESSURED
                    : ZLinkSubmitStatus.SUBMITTED,
                new ZLinkLogicalMulticastDetail(
                    detail.snapshotRemoteTargetCount(),
                    detail.admittedRemoteTargetCount(),
                    detail.droppedRemoteTargetCount(),
                    detail.unreachableRemoteTargetCount(),
                    detail.snapshotLocalSpotCount(),
                    detail.admittedLocalSpotCount(),
                    detail.droppedLocalSpotCount()));
        } catch (ZlinkSubmitException error) {
            ZLinkSubmitStatus status = switch (error.getResult()) {
                case BACKPRESSURED -> ZLinkSubmitStatus.BACKPRESSURED;
                case NOT_CONNECTED, NOT_ADMITTED -> ZLinkSubmitStatus.ROUTE_NOT_CONNECTED;
                case NOT_FOUND -> ZLinkSubmitStatus.TARGET_NOT_FOUND;
                case TERMINATED, INVALID_HANDLE -> ZLinkSubmitStatus.SHUTDOWN;
                default -> throw error;
            };
            return new ZLinkPublishResult(
                status,
                new ZLinkLogicalMulticastDetail(0, 0, 0, 0, 0, 0, 0));
        } finally {
            parts.forEach(Message::close);
        }
    }

    java.util.concurrent.CompletionStage<ZLinkPublishResult> submitAsync(
        String meshName,
        String channelName,
        String topic,
        Message payload,
        Optional<String> packetName,
        ZLinkApplicationMetadata metadata) {
        MulticastFuture result = new MulticastFuture(payload);
        if (closed) {
            result.closePayloadOnce();
            result.completeRejected(new ZLinkPublishResult(
                ZLinkSubmitStatus.SHUTDOWN,
                new ZLinkLogicalMulticastDetail(0, 0, 0, 0, 0, 0, 0)));
            return result;
        }
        try {
            multicastExecutor.execute(() -> {
                if (!result.beginCommit()) {
                    result.closePayloadOnce();
                    return;
                }
                try {
                    result.completeCommitted(submitNow(
                        meshName, channelName, topic, payload, packetName, metadata));
                } catch (RuntimeException error) {
                    result.completeExceptionallyCommitted(error);
                }
            });
        } catch (RejectedExecutionException rejected) {
            result.closePayloadOnce();
            result.completeRejected(new ZLinkPublishResult(
                closed ? ZLinkSubmitStatus.SHUTDOWN : ZLinkSubmitStatus.BACKPRESSURED,
                new ZLinkLogicalMulticastDetail(0, 0, 0, 0, 0, 0, 0)));
        }
        return result;
    }

    @Override
    public synchronized void close() {
        closed = true;
        RuntimeException firstFailure = null;
        for (ZLinkBackendSpot spot : spotsByChannel.values()) {
            try {
                spot.close();
            } catch (ZlinkCloseException ignored) {
            } catch (RuntimeException error) {
                if (firstFailure == null) {
                    firstFailure = error;
                } else {
                    firstFailure.addSuppressed(error);
                }
            }
        }
        spotsByChannel.clear();
        multicastExecutor.shutdownNow();
        if (firstFailure != null) {
            throw firstFailure;
        }
    }

    private static final class MulticastFuture
        extends CompletableFuture<ZLinkPublishResult> {
        private final Message payload;
        private boolean committed;
        private boolean payloadReleased;

        MulticastFuture(Message payload) {
            this.payload = payload;
        }

        synchronized boolean beginCommit() {
            if (isCancelled()) {
                return false;
            }
            committed = true;
            payloadReleased = true;
            return true;
        }

        @Override
        public synchronized boolean cancel(boolean mayInterruptIfRunning) {
            if (committed) {
                return false;
            }
            boolean cancelled = super.cancel(false);
            if (cancelled) {
                closePayloadOnce();
            }
            return cancelled;
        }

        synchronized void closePayloadOnce() {
            if (payloadReleased) {
                return;
            }
            payloadReleased = true;
            payload.close();
        }

        synchronized boolean completeCommitted(ZLinkPublishResult value) {
            return super.complete(value);
        }

        synchronized boolean completeExceptionallyCommitted(Throwable error) {
            return super.completeExceptionally(error);
        }

        synchronized boolean completeRejected(ZLinkPublishResult value) {
            return super.complete(value);
        }
    }

    private synchronized ZLinkBackendSpot publisherSpot(String channelName) {
        if (closed) {
            throw new ZLinkConfigurationException("SPOT publisher runtime is closed");
        }
        ZLinkInternalSpotNode node = requireChannel(channelName);
        return spotsByChannel.computeIfAbsent(channelName, ignored -> node.createSpot());
    }

    private ZLinkInternalSpotNode requireChannel(String channelName) {
        ZLinkInternalSpotNode node = nodesByChannel.get(channelName);
        if (node == null) {
            throw new ZLinkConfigurationException(
                "SPOT publisher client is not configured: " + channelName);
        }
        return node;
    }

    private void trace(
        String channelName,
        String topic,
        Optional<String> packetName) {
        ZLinkRuntimeMetrics.increment("zlink.fanout.published", Map.of());
        if (!flow.enabled(ZLinkMessageFlowOutcome.SENT)) {
            return;
        }
        flow.trace(new ZLinkMessageFlowEvent(
            ZLinkMessageFlowOutcome.SENT,
            ZLinkDispatchErrorSurface.SPOT_SUBSCRIPTION,
            ZLinkDispatchMessageKind.PUBLISH,
            packetName.orElse(null),
            channelName,
            topic,
            null,
            null,
            null,
            null,
            null));
    }
}

final class ZLinkDefaultSpotPublisherClient implements ZLinkSpotPublisherClient {
    private final ZLinkSpotPublisherRuntime publishers;

    ZLinkDefaultSpotPublisherClient(ZLinkSpotPublisherRuntime publishers) {
        this.publishers = publishers;
    }

    @Override
    public ZLinkPublishCall publish(
        String meshName,
        String channelName,
        String topic,
        Object message) {
        return publishers.publish(meshName, channelName, topic, message);
    }

    @Override
    public ZLinkPublishCall publish(
        String channelName,
        String topic,
        Object message) {
        return publishers.publish(channelName, topic, message);
    }
}

final class ZLinkExternalSpotPublishCall implements ZLinkPublishCall {
    private final systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate submitGate =
        new systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate();
    private final ZLinkSpotPublisherRuntime publishers;
    private final String meshName;
    private final String channelName;
    private final String topic;
    private final Message payload;
    private final Optional<String> packetName;
    private final ZLinkApplicationMetadata metadata;

    ZLinkExternalSpotPublishCall(
        ZLinkSpotPublisherRuntime publishers,
        String meshName,
        String channelName,
        String topic,
        Message payload,
        Optional<String> packetName) {
        this(
            publishers,
            meshName,
            channelName,
            topic,
            payload,
            packetName,
            ZLinkApplicationMetadata.empty());
    }

    private ZLinkExternalSpotPublishCall(
        ZLinkSpotPublisherRuntime publishers,
        String meshName,
        String channelName,
        String topic,
        Message payload,
        Optional<String> packetName,
        ZLinkApplicationMetadata metadata) {
        this.publishers = publishers;
        this.meshName = meshName;
        this.channelName = channelName;
        this.topic = topic;
        this.payload = payload;
        this.packetName = packetName;
        this.metadata = metadata;
    }

    public ZLinkPublishCall packetName(String packetName) {
        return new ZLinkExternalSpotPublishCall(
            publishers,
            meshName,
            channelName,
            topic,
            payload,
            Optional.of(packetName),
            metadata);
    }

    @Override
    public ZLinkPublishCall metadata(String key, String value) {
        return new ZLinkExternalSpotPublishCall(
            publishers,
            meshName,
            channelName,
            topic,
            payload,
            packetName,
            metadata.with(key, value));
    }

    @Override
    public ZLinkPublishCall metadata(Map<String, String> values) {
        return new ZLinkExternalSpotPublishCall(
            publishers,
            meshName,
            channelName,
            topic,
            payload,
            packetName,
            metadata.withAll(values));
    }

    @Override
    public java.util.concurrent.CompletionStage<ZLinkPublishResult> submit() {
        java.util.concurrent.CompletionStage<ZLinkPublishResult> duplicate =
            submitGate.begin();
        if (duplicate != null) {
            return duplicate;
        }
        return publishers.submitAsync(
            meshName, channelName, topic, payload, packetName, metadata);
    }
}

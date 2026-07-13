package systems.zlink.framework.runtime.spots;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import systems.zlink.contracts.errors.ZlinkCloseException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.runtime.internal.metrics.ZLinkRuntimeMetrics;

final class ZLinkSpotPublisherRuntime implements AutoCloseable {
    private final ZLinkMessageSerializer serializer;
    private final ZLinkSpotRouteMessages messages;
    private final ZLinkMessageFlowTracer flow;
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
        if (channelName == null || channelName.isBlank()) {
            throw new ZLinkConfigurationException("SPOT publisher channel name is required");
        }
        if (topic == null || topic.isBlank()) {
            throw new ZLinkConfigurationException("SPOT publish topic is required");
        }
        requireChannel(channelName);
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return call(
            channelName,
            topic,
            encoded.payload(),
            Optional.of(encoded.packetName()));
    }

    ZLinkPublishCall call(
        String channelName,
        String topic,
        Message payload,
        Optional<String> packetName) {
        requireChannel(channelName);
        return new ZLinkExternalSpotPublishCall(
            this, channelName, topic, payload, packetName);
    }

    void submit(
        String channelName,
        String topic,
        Message payload,
        Optional<String> packetName) {
        trace(channelName, topic, packetName);
        CompletableFuture.runAsync(() -> {
            List<Message> parts = messages.encode(packetName, payload);
            try {
                publisherSpot(channelName).publish(topic, parts, SendFlags.NONE);
            } finally {
                parts.forEach(Message::close);
            }
        }).exceptionally(error -> {
            if (closed) {
                return null;
            }
            java.util.logging.Logger.getLogger(ZLinkSpotPublisherRuntime.class.getName())
                .log(java.util.logging.Level.SEVERE, "one-way SPOT publish failed", error);
            return null;
        });
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
        if (firstFailure != null) {
            throw firstFailure;
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
        String channelName,
        String topic,
        Object message) {
        return publishers.publish(channelName, topic, message);
    }
}

final class ZLinkExternalSpotPublishCall implements ZLinkPublishCall {
    private final ZLinkSpotPublisherRuntime publishers;
    private final String channelName;
    private final String topic;
    private final Message payload;
    private final Optional<String> packetName;

    ZLinkExternalSpotPublishCall(
        ZLinkSpotPublisherRuntime publishers,
        String channelName,
        String topic,
        Message payload,
        Optional<String> packetName) {
        this.publishers = publishers;
        this.channelName = channelName;
        this.topic = topic;
        this.payload = payload;
        this.packetName = packetName;
    }

    public ZLinkPublishCall packetName(String packetName) {
        return new ZLinkExternalSpotPublishCall(
            publishers,
            channelName,
            topic,
            payload,
            Optional.of(packetName));
    }

    @Override
    public ZLinkPublishCall metadata(String key, String value) {
        return this;
    }

    @Override
    public void submit() {
        publishers.submit(channelName, topic, payload, packetName);
    }
}

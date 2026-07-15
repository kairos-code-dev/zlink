package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddress;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddressResolver;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.spots.ZLinkSpotOutbound;

final class DefaultSpotOutbound implements ZLinkSpotOutbound {
    private final ZLinkBackendSpot backendSpot;
    private final String publisherChannelName;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkSpotRoutedOutbound routed;
    private final ZLinkSpotDirectOutbound direct;
    private final ZLinkSpotPublisherRuntime publishers;
    private final ZLinkChannelRuntime channels;
    private final boolean routeMeshEnabled;
    private final Duration defaultRequestTimeout;
    private final Supplier<SpotTransportAddressResolver> spotAddressResolver;

    DefaultSpotOutbound(
        ZLinkBackendSpot backendSpot,
        String publisherChannelName,
        ZLinkMessageSerializer serializer,
        ZLinkSpotRoutedOutbound routed,
        ZLinkSpotDirectOutbound direct,
        ZLinkSpotPublisherRuntime publishers,
        ZLinkChannelRuntime channels,
        boolean routeMeshEnabled,
        Duration defaultRequestTimeout,
        Supplier<SpotTransportAddressResolver> spotAddressResolver) {
        this.backendSpot = backendSpot;
        this.publisherChannelName = publisherChannelName;
        this.serializer = serializer;
        this.routed = routed;
        this.direct = direct;
        this.publishers = publishers;
        this.channels = channels;
        this.routeMeshEnabled = routeMeshEnabled;
        this.defaultRequestTimeout = defaultRequestTimeout;
        this.spotAddressResolver = spotAddressResolver;
    }

    @Override
    public ZLinkSendCall sendToSpot(SpotHandle spot, Object message) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return new DeferredSpotSendCall(spot, encoded.payload(), Optional.of(encoded.packetName()));
    }

    @Override
    public ZLinkRequestCall requestToSpot(SpotHandle spot, Object request) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, request);
        return new DeferredSpotRequestCall(
            spot, encoded.payload(), Optional.of(encoded.packetName()), defaultRequestTimeout);
    }

    @Override
    public ZLinkPublishCall publish(String topic, Object message) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        if (publisherChannelName != null && publishers.contains(publisherChannelName)) {
            return publishers.call(
                publisherChannelName,
                topic,
                encoded.payload(),
                Optional.of(encoded.packetName()));
        }
        return direct.publish(
            backendSpot,
            topic,
            encoded.payload(),
            Optional.of(encoded.packetName()));
    }

    @Override
    public ZLinkSendCall sendToChannel(String channelName, Object message) {
        requireChannels(channelName);
        return channels.sendToChannel(channelName, message);
    }

    @Override
    public ZLinkRequestCall requestToChannel(String channelName, Object request) {
        requireChannels(channelName);
        return channels.requestToChannel(channelName, request);
    }

    private void requireChannels(String channelName) {
        if (channels == null) {
            throw new ZLinkConfigurationException(
                "channel client is not configured: " + channelName);
        }
    }

    private static void requireRoutingId(RoutingId routingId) {
        if (routingId == null || routingId.size() == 0) {
            throw new ZLinkConfigurationException("routing id is required");
        }
    }

    private CompletionStage<SpotTransportAddress> resolve(SpotHandle handle) {
        SpotTransportAddressResolver resolver;
        try {
            resolver = spotAddressResolver == null ? null : spotAddressResolver.get();
        } catch (RuntimeException ignored) {
            resolver = null;
        }
        if (resolver == null) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "SpotHandle resolver is not configured"));
        }
        return resolver.resolve(handle).thenCompose(value -> value
            .map(CompletableFuture::completedFuture)
            .orElseGet(() -> CompletableFuture.failedFuture(
                new ZLinkConfigurationException("SpotHandle cannot be resolved"))));
    }

    private final class DeferredSpotSendCall implements ZLinkSendCall {
        private final SpotHandle target;
        private final systems.zlink.contracts.messaging.Message payload;
        private final Optional<String> packetName;

        private DeferredSpotSendCall(SpotHandle target, systems.zlink.contracts.messaging.Message payload,
            Optional<String> packetName) {
            this.target = java.util.Objects.requireNonNull(target, "target");
            this.payload = payload;
            this.packetName = packetName;
        }

        @Override public void submit() {
            resolve(target).thenAccept(address -> {
                ZLinkSendCall call = routeMeshEnabled
                    ? routed.send(address.routerChannelId(), address.targetNodeRid(), address.spotRid(), payload, packetName)
                    : direct.send(backendSpot, address.targetNodeRid(), address.spotRid(), payload, packetName);
                call.submit();
            });
        }
    }

    private final class DeferredSpotRequestCall implements ZLinkRequestCall {
        private final SpotHandle target;
        private final systems.zlink.contracts.messaging.Message payload;
        private final Optional<String> packetName;
        private final Duration timeout;

        private DeferredSpotRequestCall(SpotHandle target, systems.zlink.contracts.messaging.Message payload,
            Optional<String> packetName, Duration timeout) {
            this.target = java.util.Objects.requireNonNull(target, "target");
            this.payload = payload;
            this.packetName = packetName;
            this.timeout = timeout;
        }

        @Override public ZLinkRequestCall timeout(Duration value) {
            return new DeferredSpotRequestCall(target, payload, packetName, value);
        }
        @Override public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
            CompletionStage<TReply> stage = resolve(target).thenCompose(address -> {
                ZLinkRequestCall call = routeMeshEnabled
                    ? routed.request(address.routerChannelId(), address.targetNodeRid(), address.spotRid(), payload, packetName, timeout)
                    : direct.request(backendSpot, address.targetNodeRid(), address.spotRid(), payload, packetName, timeout);
                return call.submit(replyType);
            });
            return systems.zlink.framework.execution.ZLinkAsyncSerialQueue.manageCurrent(stage);
        }
    }
}

package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.util.Map;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.runtime.messaging.ZLinkApplicationMetadata;

import systems.zlink.framework.runtime.internal.spots.SpotTransportAddress;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddressResolver;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.spots.ZLinkSpotOutbound;

final class DefaultSpotOutbound implements ZLinkSpotOutbound {
    private final ZLinkBackendSpot backendSpot;
    private final String meshName;
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
        String meshName,
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
        this.meshName = meshName;
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
    public systems.zlink.framework.spots.ZLinkSpotSendCall sendToSpot(
        String spotId,
        Object message) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return new DeferredSpotSendCall(spotId, encoded.payload(), Optional.of(encoded.packetName()));
    }

    @Override
    public systems.zlink.framework.spots.ZLinkSpotRequestCall requestToSpot(
        String spotId,
        Object request) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, request);
        return new DeferredSpotRequestCall(
            spotId, encoded.payload(), Optional.of(encoded.packetName()), defaultRequestTimeout);
    }

    @Override
    public ZLinkPublishCall publish(
        String channelName,
        String topic,
        Object message) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        if (publisherChannelName != null && publishers.contains(publisherChannelName)) {
            return publishers.call(
                publisherChannelName,
                channelName,
                topic,
                encoded.payload(),
                Optional.of(encoded.packetName()));
        }
        return direct.publish(
            backendSpot,
            channelName,
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
        if (channels == null || meshName == null) {
            throw new ZLinkConfigurationException(
                "channel client is not configured: " + channelName);
        }
    }

    private static void requireRoutingId(RoutingId routingId) {
        if (routingId == null || routingId.size() == 0) {
            throw new ZLinkConfigurationException("routing id is required");
        }
    }

    private CompletionStage<SpotTransportAddress> resolve(String spotId) {
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
        return resolver.resolve(spotId).thenCompose(value -> value
            .map(CompletableFuture::completedFuture)
            .orElseGet(() -> CompletableFuture.failedFuture(
                new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SPOT_ROUTE_NOT_FOUND,
                    "SpotHandle route is stale or unavailable"))));
    }

    private final class DeferredSpotSendCall
        implements systems.zlink.framework.spots.ZLinkSpotSendCall {
        private final java.util.concurrent.atomic.AtomicBoolean submitGate =
            new java.util.concurrent.atomic.AtomicBoolean();
        private final String target;
        private final systems.zlink.contracts.messaging.Message payload;
        private final Optional<String> packetName;
        private final ZLinkApplicationMetadata metadata;

        private DeferredSpotSendCall(String target, systems.zlink.contracts.messaging.Message payload,
            Optional<String> packetName) {
            this(target, payload, packetName, ZLinkApplicationMetadata.empty());
        }

        private DeferredSpotSendCall(
            String target,
            systems.zlink.contracts.messaging.Message payload,
            Optional<String> packetName,
            ZLinkApplicationMetadata metadata) {
            this.target = java.util.Objects.requireNonNull(target, "target");
            this.payload = payload;
            this.packetName = packetName;
            this.metadata = metadata;
        }

        @Override
        public systems.zlink.framework.spots.ZLinkSpotSendCall metadata(
            String key,
            String value) {
            return new DeferredSpotSendCall(
                target, payload, packetName, metadata.with(key, value));
        }

        @Override
        public systems.zlink.framework.spots.ZLinkSpotSendCall metadata(
            Map<String, String> values) {
            return new DeferredSpotSendCall(
                target, payload, packetName, metadata.withAll(values));
        }

        @Override public CompletionStage<Void> submit() {
            CompletionStage<Void> duplicate =
                ZLinkOneWayCalls.beginOneWay(submitGate);
            if (duplicate != null) {
                return duplicate;
            }
            return resolve(target).thenCompose(address -> {
                backendSpot.rememberSpotAuthority(
                    address.targetNodeRid(),
                    address.spotId(),
                    address.spotGeneration(),
                    address.authorityOwnerGeneration());
                ZLinkSendCall call = routeMeshEnabled
                    ? routed.send(address.routerChannelId(), address.targetNodeRid(), address.spotId(),
                        address.spotGeneration(), payload, packetName)
                    : direct.send(backendSpot, address.targetNodeRid(), address.spotId(),
                        address.spotGeneration(), payload, packetName);
                call = call.metadata(metadata.values());
                return call.submit();
            });
        }
    }

    private final class DeferredSpotRequestCall
        implements systems.zlink.framework.spots.ZLinkSpotRequestCall {
        private final String target;
        private final systems.zlink.contracts.messaging.Message payload;
        private final Optional<String> packetName;
        private final Duration timeout;
        private final ZLinkApplicationMetadata metadata;

        private DeferredSpotRequestCall(String target, systems.zlink.contracts.messaging.Message payload,
            Optional<String> packetName, Duration timeout) {
            this(
                target,
                payload,
                packetName,
                timeout,
                ZLinkApplicationMetadata.empty());
        }

        private DeferredSpotRequestCall(
            String target,
            systems.zlink.contracts.messaging.Message payload,
            Optional<String> packetName,
            Duration timeout,
            ZLinkApplicationMetadata metadata) {
            this.target = java.util.Objects.requireNonNull(target, "target");
            this.payload = payload;
            this.packetName = packetName;
            this.timeout = timeout;
            this.metadata = metadata;
        }

        @Override
        public systems.zlink.framework.spots.ZLinkSpotRequestCall metadata(
            String key,
            String value) {
            return new DeferredSpotRequestCall(
                target, payload, packetName, timeout, metadata.with(key, value));
        }

        @Override
        public systems.zlink.framework.spots.ZLinkSpotRequestCall metadata(
            Map<String, String> values) {
            return new DeferredSpotRequestCall(
                target, payload, packetName, timeout, metadata.withAll(values));
        }

        @Override
        public systems.zlink.framework.spots.ZLinkSpotRequestCall timeout(
            Duration value) {
            return new DeferredSpotRequestCall(
                target, payload, packetName, value, metadata);
        }
        @Override public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
            systems.zlink.framework.runtime.internal.handlers
                .ZLinkSuspendInvocationContext.rejectSameSpotWait(target);
            CompletionStage<TReply> stage = resolve(target).thenCompose(address -> {
                backendSpot.rememberSpotAuthority(
                    address.targetNodeRid(),
                    address.spotId(),
                    address.spotGeneration(),
                    address.authorityOwnerGeneration());
                ZLinkRequestCall call = routeMeshEnabled
                    ? routed.request(address.routerChannelId(), address.targetNodeRid(), address.spotId(),
                        address.spotGeneration(), payload, packetName, timeout)
                    : direct.request(backendSpot, address.targetNodeRid(), address.spotId(),
                        address.spotGeneration(), payload, packetName, timeout);
                return call.metadata(metadata.values()).submit(replyType);
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
}

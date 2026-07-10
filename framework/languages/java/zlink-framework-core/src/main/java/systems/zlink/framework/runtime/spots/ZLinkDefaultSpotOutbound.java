package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkYieldRequestCall;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.locations.SpotRef;
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

    DefaultSpotOutbound(
        ZLinkBackendSpot backendSpot,
        String publisherChannelName,
        ZLinkMessageSerializer serializer,
        ZLinkSpotRoutedOutbound routed,
        ZLinkSpotDirectOutbound direct,
        ZLinkSpotPublisherRuntime publishers,
        ZLinkChannelRuntime channels,
        boolean routeMeshEnabled,
        Duration defaultRequestTimeout) {
        this.backendSpot = backendSpot;
        this.publisherChannelName = publisherChannelName;
        this.serializer = serializer;
        this.routed = routed;
        this.direct = direct;
        this.publishers = publishers;
        this.channels = channels;
        this.routeMeshEnabled = routeMeshEnabled;
        this.defaultRequestTimeout = defaultRequestTimeout;
    }

    @Override
    public ZLinkSendCall sendToSpot(SpotRef spotRef, Object message) {
        requireRoutingId(spotRef.nodeRid());
        requireRoutingId(spotRef.spotRid());
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        if (routeMeshEnabled) {
            return routed.send(
                spotRef.meshName(),
                spotRef.nodeRid(),
                spotRef.spotRid(),
                encoded.payload(),
                Optional.of(encoded.packetName()));
        }
        return direct.send(
            backendSpot,
            spotRef.nodeRid(),
            spotRef.spotRid(),
            encoded.payload(),
            Optional.of(encoded.packetName()));
    }

    @Override
    public ZLinkYieldRequestCall requestToSpot(SpotRef spotRef, Object request) {
        requireRoutingId(spotRef.nodeRid());
        requireRoutingId(spotRef.spotRid());
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, request);
        if (routeMeshEnabled) {
            return routed.request(
                spotRef.meshName(),
                spotRef.nodeRid(),
                spotRef.spotRid(),
                encoded.payload(),
                Optional.of(encoded.packetName()),
                defaultRequestTimeout);
        }
        return direct.request(
            backendSpot,
            spotRef.nodeRid(),
            spotRef.spotRid(),
            encoded.payload(),
            Optional.of(encoded.packetName()),
            defaultRequestTimeout);
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
    public ZLinkYieldRequestCall requestToChannel(String channelName, Object request) {
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
}

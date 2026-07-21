package systems.zlink.framework.runtime.spots;

import java.util.Map;
import java.util.Optional;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkSendContext;

record ZLinkSpotSendHandlerContext(
    String packet,
    String content,
    Map<String, String> metadata) implements ZLinkSendContext {
    ZLinkSpotSendHandlerContext {
        metadata = Map.copyOf(metadata);
    }

    @Override public Optional<String> channelName() { return Optional.empty(); }
    @Override public Optional<String> packetName() { return Optional.of(packet); }
    @Override public Optional<String> contentType() { return Optional.ofNullable(content); }
}

record ZLinkSpotRequestHandlerContext(
    String packet,
    String content,
    Map<String, String> metadata) implements ZLinkRequestContext {
    ZLinkSpotRequestHandlerContext {
        metadata = Map.copyOf(metadata);
    }

    @Override public Optional<String> channelName() { return Optional.empty(); }
    @Override public Optional<String> packetName() { return Optional.of(packet); }
    @Override public Optional<String> contentType() { return Optional.ofNullable(content); }
}

record ZLinkSpotPublishHandlerContext(
    String channel,
    String packet,
    String topic,
    String content,
    Optional<String> source,
    Map<String, String> metadata) implements ZLinkPublishContext {
    ZLinkSpotPublishHandlerContext {
        source = source == null ? Optional.empty() : source;
        metadata = Map.copyOf(metadata);
    }

    @Override public Optional<String> channelName() {
        return Optional.ofNullable(channel).filter(value -> !value.isBlank());
    }
    @Override public Optional<String> packetName() { return Optional.of(packet); }
    @Override public Optional<String> contentType() { return Optional.ofNullable(content); }
}

package systems.zlink.framework.runtime.spots;

import java.util.Optional;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;

record ZLinkSpotActorSendHandlerContext(String packet) implements ZLinkSpotActorSendContext {
    private static final CancellationToken NONE = () -> false;

    @Override public Optional<String> channelName() { return Optional.empty(); }
    @Override public Optional<String> packetName() { return Optional.ofNullable(packet); }
    @Override public Optional<String> contentType() { return Optional.empty(); }
    @Override public CancellationToken cancellationToken() { return NONE; }
}

record ZLinkSpotActorRequestHandlerContext(String packet) implements ZLinkSpotActorRequestContext {
    private static final CancellationToken NONE = () -> false;

    @Override public Optional<String> channelName() { return Optional.empty(); }
    @Override public Optional<String> packetName() { return Optional.ofNullable(packet); }
    @Override public Optional<String> contentType() { return Optional.empty(); }
    @Override public CancellationToken cancellationToken() { return NONE; }
}

package systems.zlink.framework;

import java.util.Optional;

/**
 * Common context visible to application handlers.
 */
public interface ZLinkHandlerContext {
    Optional<String> channelName();

    Optional<String> packetName();

    Optional<String> contentType();

    CancellationToken cancellationToken();
}

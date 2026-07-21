package systems.zlink.framework;

import java.util.Map;
import java.util.Optional;

/**
 * Common context visible to application handlers.
 */
public interface ZLinkHandlerContext {
    Optional<String> channelName();

    Optional<String> packetName();

    Optional<String> contentType();

    Map<String, String> metadata();
}

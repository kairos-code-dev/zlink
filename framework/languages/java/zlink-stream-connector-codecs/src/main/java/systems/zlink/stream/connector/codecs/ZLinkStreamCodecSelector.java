package systems.zlink.stream.connector.codecs;

import java.util.Optional;

public interface ZLinkStreamCodecSelector {
    Optional<ZLinkStreamCodec> find(String contentType);
}

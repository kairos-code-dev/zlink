package systems.zlink.stream.connector;

import java.util.Map;

public interface ZLinkStreamSendCall {
    ZLinkStreamSendCall packetName(String name);

    ZLinkStreamSendCall metadata(String key, String value);

    ZLinkStreamSendCall metadata(Map<String, String> metadata);

    ZLinkStreamSendCall compress();

    void submit();
}

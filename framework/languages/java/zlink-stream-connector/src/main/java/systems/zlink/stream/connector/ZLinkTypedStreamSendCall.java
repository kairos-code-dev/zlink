package systems.zlink.stream.connector;

import java.util.Map;

public interface ZLinkTypedStreamSendCall {
    ZLinkTypedStreamSendCall packetName(String name);

    ZLinkTypedStreamSendCall metadata(String key, String value);

    ZLinkTypedStreamSendCall metadata(Map<String, String> metadata);

    ZLinkTypedStreamSendCall compress();

    void submit();
}

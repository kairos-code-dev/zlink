package systems.zlink.stream.connector.codecs;

import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;

public interface ZLinkStreamCodec {
    <T> ZLinkStreamEncodedPayload encode(String packetName, T value);

    <T> T decode(ZLinkStreamEncodedPayload payload, Class<T> type);
}

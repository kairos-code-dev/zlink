package systems.zlink.samples.gamequest.server.gameapi.session;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.stream.connector.ZLinkStreamCodec;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;
import systems.zlink.stream.connector.json.ZLinkStreamJson;

/** Decodes inbound JSON stream packets into typed session requests. */
public final class StreamPayloads {
    private StreamPayloads() {
    }

    public static <T> T decode(ZLinkStreamHeader header, Message payload, Class<T> type) {
        return ZLinkStreamJson.decode(
            new ZLinkStreamEncodedPayload(
                header.packetName(),
                payload,
                header.metadata(),
                connectorCodec(header)),
            type);
    }

    private static ZLinkStreamCodec connectorCodec(ZLinkStreamHeader header) {
        return switch (header.codec()) {
            case RAW -> ZLinkStreamCodec.RAW;
            case JSON -> ZLinkStreamCodec.JSON;
            case MESSAGE_PACK -> ZLinkStreamCodec.MESSAGE_PACK;
            case PROTOBUF -> ZLinkStreamCodec.PROTOBUF;
        };
    }
}

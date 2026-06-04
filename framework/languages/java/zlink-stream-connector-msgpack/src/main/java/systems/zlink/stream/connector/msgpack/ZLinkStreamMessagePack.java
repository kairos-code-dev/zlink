package systems.zlink.stream.connector.msgpack;

import java.nio.charset.StandardCharsets;
import java.util.Map;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;
import systems.zlink.stream.connector.ZLinkStreamMessage;
import systems.zlink.stream.connector.ZLinkStreamMessageHandler;
import systems.zlink.stream.connector.ZLinkStreamRequestCall;
import systems.zlink.stream.connector.ZLinkStreamSendCall;

public final class ZLinkStreamMessagePack {
    public static final String CONTENT_TYPE = "application/msgpack";

    private ZLinkStreamMessagePack() {
    }

    public static ZLinkStreamSendCall send(
        ZLinkStreamConnector connector,
        Object payload) {
        return connector.send(encode(packetName(connector, payload), payload));
    }

    public static ZLinkStreamRequestCall request(
        ZLinkStreamConnector connector,
        Object payload) {
        return connector.request(encode(packetName(connector, payload), payload));
    }

    public static <TPayload> AutoCloseable on(
        ZLinkStreamConnector connector,
        Class<TPayload> payloadType,
        ZLinkStreamMessageHandler<TPayload> handler) {
        return on(
            connector,
            connector.options().nameResolver().resolve(payloadType),
            payloadType,
            handler);
    }

    public static <TPayload> AutoCloseable on(
        ZLinkStreamConnector connector,
        String name,
        Class<TPayload> payloadType,
        ZLinkStreamMessageHandler<TPayload> handler) {
        return connector.on(name, message -> handler.handleAsync(new ZLinkStreamMessage<>(
            message.packetName(),
            decode(message.payload(), payloadType),
            message.metadata())));
    }

    public static ZLinkStreamEncodedPayload encode(String packetName, Object value) {
        return new ZLinkStreamEncodedPayload(
            packetName,
            Message.from(encodeBytes(value)),
            Map.of("content-type", CONTENT_TYPE));
    }

    public static <T> T decode(ZLinkStreamEncodedPayload payload, Class<T> type) {
        if (type == String.class) {
            return type.cast(payload.payload().toUtf8String());
        }
        if (type == byte[].class) {
            return type.cast(payload.payload().toByteArray());
        }
        if (type == Message.class) {
            return type.cast(Message.from(payload.payload()));
        }
        throw new IllegalArgumentException("unsupported MessagePack payload type: " + type.getName());
    }

    private static byte[] encodeBytes(Object value) {
        if (value instanceof byte[] bytes) {
            return bytes;
        }
        if (value instanceof Message message) {
            return message.toByteArray();
        }
        return String.valueOf(value).getBytes(StandardCharsets.UTF_8);
    }

    private static String packetName(ZLinkStreamConnector connector, Object payload) {
        return payload == null
            ? "Null"
            : connector.options().nameResolver().resolve(payload.getClass());
    }
}

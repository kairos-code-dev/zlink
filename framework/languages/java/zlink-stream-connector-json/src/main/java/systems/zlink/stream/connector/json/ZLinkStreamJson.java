package systems.zlink.stream.connector.json;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
import java.io.IOException;
import java.util.Map;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;
import systems.zlink.stream.connector.ZLinkStreamMessage;
import systems.zlink.stream.connector.ZLinkStreamMessageHandler;
import systems.zlink.stream.connector.ZLinkStreamRequestCall;
import systems.zlink.stream.connector.ZLinkStreamSendCall;

public final class ZLinkStreamJson {
    public static final String CONTENT_TYPE = "application/json";
    private static final ObjectMapper Mapper = JsonMapper.builder()
        .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
        .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
        .findAndAddModules()
        .build();

    private ZLinkStreamJson() {
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
        if (type == byte[].class) {
            return type.cast(payload.payload().toByteArray());
        }
        if (type == Message.class) {
            return type.cast(Message.from(payload.payload()));
        }
        try {
            return Mapper.readValue(payload.payload().toByteArray(), type);
        } catch (IOException ex) {
            throw new IllegalArgumentException(
                "failed to deserialize JSON stream payload as " + type.getName(),
                ex);
        }
    }

    private static byte[] encodeBytes(Object value) {
        if (value instanceof byte[] bytes) {
            return bytes;
        }
        if (value instanceof Message message) {
            return message.toByteArray();
        }
        try {
            return Mapper.writeValueAsBytes(value);
        } catch (JsonProcessingException ex) {
            throw new IllegalArgumentException(
                "failed to serialize JSON stream payload: " + valueTypeName(value),
                ex);
        }
    }

    private static String packetName(ZLinkStreamConnector connector, Object payload) {
        return payload == null
            ? "Null"
            : connector.options().nameResolver().resolve(payload.getClass());
    }

    private static String valueTypeName(Object value) {
        return value == null ? "null" : value.getClass().getName();
    }
}

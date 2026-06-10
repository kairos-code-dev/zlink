package systems.zlink.stream.connector.json;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
import java.io.IOException;
import java.time.Duration;
import java.util.Map;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamCodec;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;
import systems.zlink.stream.connector.ZLinkStreamMessage;
import systems.zlink.stream.connector.ZLinkStreamMessageHandler;
import systems.zlink.stream.connector.ZLinkStreamRequestCall;
import systems.zlink.stream.connector.ZLinkStreamSendCall;
import systems.zlink.stream.connector.ZLinkStreamTypedCodec;

public final class ZLinkStreamJson {
    public static final String CONTENT_TYPE = "application/json";
    private static final ObjectMapper Mapper = JsonMapper.builder()
        .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
        .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
        .findAndAddModules()
        .build();

    private ZLinkStreamJson() {
    }

    public static ZLinkStreamTypedCodec codec() {
        return JsonCodec.INSTANCE;
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

    public static <TPayload> CompletionStage<ZLinkStreamMessage<TPayload>> waitForAsync(
        ZLinkStreamConnector connector,
        Class<TPayload> payloadType) {
        return waitForAsync(
            connector,
            connector.options().nameResolver().resolve(payloadType),
            payloadType);
    }

    public static <TPayload> CompletionStage<ZLinkStreamMessage<TPayload>> waitForAsync(
        ZLinkStreamConnector connector,
        Class<TPayload> payloadType,
        Duration timeout) {
        return waitForAsync(
            connector,
            connector.options().nameResolver().resolve(payloadType),
            payloadType,
            timeout);
    }

    public static <TPayload> CompletionStage<ZLinkStreamMessage<TPayload>> waitForAsync(
        ZLinkStreamConnector connector,
        String name,
        Class<TPayload> payloadType,
        Duration timeout) {
        return connector.waitFor(name)
            .timeout(timeout)
            .submit()
            .thenApply(message -> decodeMessage(message, payloadType));
    }

    public static <TPayload> CompletionStage<ZLinkStreamMessage<TPayload>> waitForAsync(
        ZLinkStreamConnector connector,
        String name,
        Class<TPayload> payloadType) {
        return connector.waitFor(name)
            .submit()
            .thenApply(message -> decodeMessage(message, payloadType));
    }

    public static ZLinkStreamEncodedPayload encode(String packetName, Object value) {
        return new ZLinkStreamEncodedPayload(
            packetName,
            Message.from(encodeBytes(value)),
            Map.of(),
            ZLinkStreamCodec.JSON);
    }

    public static <T> T decode(ZLinkStreamEncodedPayload payload, Class<T> type) {
        if (payload.codec() != ZLinkStreamCodec.JSON) {
            throw new IllegalArgumentException(
                "stream payload codec is " + payload.codec() + ", not JSON");
        }
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

    private static <TPayload> ZLinkStreamMessage<TPayload> decodeMessage(
        ZLinkStreamMessage<ZLinkStreamEncodedPayload> message,
        Class<TPayload> payloadType) {
        return new ZLinkStreamMessage<>(
            message.packetName(),
            decode(message.payload(), payloadType),
            message.metadata());
    }

    private enum JsonCodec implements ZLinkStreamTypedCodec {
        INSTANCE;

        @Override
        public <T> ZLinkStreamEncodedPayload encode(String packetName, T value) {
            return ZLinkStreamJson.encode(packetName, value);
        }

        @Override
        public <T> T decode(ZLinkStreamEncodedPayload payload, Class<T> type) {
            return ZLinkStreamJson.decode(payload, type);
        }
    }
}

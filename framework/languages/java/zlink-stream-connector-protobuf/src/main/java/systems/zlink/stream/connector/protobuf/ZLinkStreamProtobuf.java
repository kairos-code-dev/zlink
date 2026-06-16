package systems.zlink.stream.connector.protobuf;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protobuf.MessageLite;
import com.google.protobuf.Parser;
import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamCodec;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;
import systems.zlink.stream.connector.ZLinkStreamMessage;
import systems.zlink.stream.connector.ZLinkStreamMessageHandler;
import systems.zlink.stream.connector.ZLinkStreamRequestCall;
import systems.zlink.stream.connector.ZLinkStreamSendCall;
import systems.zlink.stream.connector.ZLinkStreamTypedCodec;

public final class ZLinkStreamProtobuf {
    public static final String CONTENT_TYPE = "application/protobuf";
    private static final ObjectMapper MAPPER = JsonMapper.builder()
        .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
        .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
        .findAndAddModules()
        .build();

    private ZLinkStreamProtobuf() {
    }

    public static ZLinkStreamTypedCodec codec() {
        return ProtobufCodec.INSTANCE;
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
            Map.of(),
            ZLinkStreamCodec.PROTOBUF);
    }

    public static <T> T decode(ZLinkStreamEncodedPayload payload, Class<T> type) {
        if (payload.codec() != ZLinkStreamCodec.PROTOBUF) {
            throw new IllegalArgumentException(
                "stream payload codec is " + payload.codec() + ", not PROTOBUF");
        }
        if (type == String.class) {
            return type.cast(payload.payload().toUtf8String());
        }
        if (type == byte[].class) {
            return type.cast(payload.payload().toByteArray());
        }
        if (type == Message.class) {
            return type.cast(Message.from(payload.payload()));
        }
        if (MessageLite.class.isAssignableFrom(type)) {
            return parseProtobuf(payload, type);
        }
        try {
            return MAPPER.readValue(payload.payload().toByteArray(), type);
        } catch (IOException ex) {
            throw new IllegalArgumentException(
                "failed to decode Protobuf payload as " + type.getName(),
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
        if (value instanceof MessageLite protobuf) {
            return protobuf.toByteArray();
        }
        if (value instanceof String text) {
            return text.getBytes(StandardCharsets.UTF_8);
        }
        try {
            return MAPPER.writeValueAsBytes(value);
        } catch (JsonProcessingException ex) {
            throw new IllegalArgumentException(
                "failed to encode Protobuf payload: " + valueTypeName(value),
                ex);
        }
    }

    private static String valueTypeName(Object value) {
        return value == null ? "null" : value.getClass().getName();
    }

    private static <T> T parseProtobuf(ZLinkStreamEncodedPayload payload, Class<T> type) {
        try {
            Parser<?> parser = parserFor(type);
            return type.cast(parser.parseFrom(payload.payload().toByteArray()));
        } catch (InvalidProtocolBufferException ex) {
            throw new IllegalArgumentException(
                "failed to decode Protobuf payload as " + type.getName(),
                ex);
        }
    }

    private static Parser<?> parserFor(Class<?> type) {
        try {
            Method parserMethod = type.getMethod("parser");
            Object parser = parserMethod.invoke(null);
            if (parser instanceof Parser<?> typedParser) {
                return typedParser;
            }
            throw new IllegalArgumentException(
                "Protobuf type parser() did not return Parser: " + type.getName());
        } catch (NoSuchMethodException ex) {
            throw new IllegalArgumentException(
                "Protobuf type does not expose parser(): " + type.getName(),
                ex);
        } catch (IllegalAccessException ex) {
            throw new IllegalArgumentException(
                "Protobuf parser() is not accessible: " + type.getName(),
                ex);
        } catch (InvocationTargetException ex) {
            throw new IllegalArgumentException(
                "Protobuf parser() failed for " + type.getName(),
                ex.getCause());
        }
    }

    private static String packetName(ZLinkStreamConnector connector, Object payload) {
        return payload == null
            ? "Null"
            : connector.options().nameResolver().resolve(payload.getClass());
    }

    private enum ProtobufCodec implements ZLinkStreamTypedCodec {
        INSTANCE;

        @Override
        public <T> ZLinkStreamEncodedPayload encode(String packetName, T value) {
            return ZLinkStreamProtobuf.encode(packetName, value);
        }

        @Override
        public <T> T decode(ZLinkStreamEncodedPayload payload, Class<T> type) {
            return ZLinkStreamProtobuf.decode(payload, type);
        }
    }
}

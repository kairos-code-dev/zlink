package systems.zlink.framework.codecs.protobuf;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
import com.google.protobuf.MessageLite;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkEncodedPayload;
import systems.zlink.stream.connector.ZLinkStreamCodec;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;
import systems.zlink.stream.connector.ZLinkStreamTypedCodec;

enum ZLinkProtobufStreamCodec implements ZLinkStreamTypedCodec {
    INSTANCE;

    private static final ObjectMapper MAPPER = JsonMapper.builder()
        .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
        .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
        .findAndAddModules()
        .build();

    @Override
    public <T> ZLinkStreamEncodedPayload encode(String packetName, T value) {
        return new ZLinkStreamEncodedPayload(
            packetName,
            Message.from(encodeBytes(value)),
            Map.of(),
            ZLinkStreamCodec.PROTOBUF);
    }

    @Override
    public <T> T decode(ZLinkStreamEncodedPayload payload, Class<T> type) {
        if (payload.codec() != ZLinkStreamCodec.PROTOBUF) {
            throw new IllegalArgumentException(
                "stream payload codec is " + payload.codec() + ", not PROTOBUF");
        }
        if (type == String.class) {
            return type.cast(payload.payload().toUtf8String());
        }
        if (type == Message.class || type == byte[].class) {
            rejectRawPayloadType();
        }
        if (ZLinkProtobufMessageSerializer.canSerialize(type)) {
            return ZLinkProtobufMessageSerializer.INSTANCE.deserialize(
                ZLinkEncodedPayload.from(payload.payload().toByteArray()),
                type);
        }
        try {
            return MAPPER.readValue(payload.payload().toByteArray(), type);
        } catch (IOException ex) {
            throw new IllegalArgumentException(
                "failed to decode Protobuf stream payload as " + type.getName(),
                ex);
        }
    }

    private static byte[] encodeBytes(Object value) {
        if (value instanceof byte[] || value instanceof Message) {
            rejectRawPayloadType();
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
                "failed to encode Protobuf stream payload: " + valueTypeName(value),
                ex);
        }
    }

    private static String valueTypeName(Object value) {
        return value == null ? "null" : value.getClass().getName();
    }

    private static void rejectRawPayloadType() {
        throw new IllegalArgumentException(
            "binding Message and byte[] are not framework business payload types; use a DTO, ZLinkMessage, or ZLinkEncodedPayload");
    }
}

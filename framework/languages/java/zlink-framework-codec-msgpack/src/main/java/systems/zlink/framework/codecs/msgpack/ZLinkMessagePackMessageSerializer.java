package systems.zlink.framework.codecs.msgpack;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkEncodedPayload;
import systems.zlink.framework.ZLinkMessageSerializer;

final class ZLinkMessagePackMessageSerializer implements ZLinkMessageSerializer {
    static final ZLinkMessagePackMessageSerializer INSTANCE = new ZLinkMessagePackMessageSerializer();

    private static final ObjectMapper MAPPER = JsonMapper.builder()
        .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
        .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
        .findAndAddModules()
        .build();

    private ZLinkMessagePackMessageSerializer() {
    }

    @Override
    public <T> ZLinkEncodedPayload serialize(T value) {
        rejectRawPayloadType(value == null ? null : value.getClass());
        return ZLinkEncodedPayload.from(encodeBytes(value));
    }

    @Override
    public <T> T deserialize(ZLinkEncodedPayload payload, Class<T> type) {
        if (type == String.class) {
            return type.cast(new String(payload.bytes(), StandardCharsets.UTF_8));
        }
        rejectRawPayloadType(type);
        try {
            return MAPPER.readValue(payload.bytes(), type);
        } catch (IOException ex) {
            throw new IllegalArgumentException(
                "failed to decode MessagePack payload as " + type.getName(),
                ex);
        }
    }

    private static byte[] encodeBytes(Object value) {
        if (value instanceof String text) {
            return text.getBytes(StandardCharsets.UTF_8);
        }
        try {
            return MAPPER.writeValueAsBytes(value);
        } catch (JsonProcessingException ex) {
            throw new IllegalArgumentException(
                "failed to encode MessagePack payload: " + valueTypeName(value),
                ex);
        }
    }

    private static String valueTypeName(Object value) {
        return value == null ? "null" : value.getClass().getName();
    }

    private static void rejectRawPayloadType(Class<?> type) {
        if (type == Message.class || type == byte[].class) {
            throw new IllegalArgumentException(
                "binding Message and byte[] are not framework business payload types; use a DTO, ZLinkMessage, or ZLinkEncodedPayload");
        }
    }
}

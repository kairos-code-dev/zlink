package systems.zlink.framework.runtime.messaging;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
import java.io.IOException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkEncodedPayload;
import systems.zlink.framework.ZLinkMessageSerializer;

public final class ZLinkJsonMessageSerializer implements ZLinkMessageSerializer {
    private final ObjectMapper mapper;

    public ZLinkJsonMessageSerializer() {
        this(JsonMapper.builder()
            .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
            .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
            .findAndAddModules()
            .build());
    }

    ZLinkJsonMessageSerializer(ObjectMapper mapper) {
        this.mapper = mapper;
    }

    @Override
    public <T> ZLinkEncodedPayload serialize(T value) {
        rejectRawPayloadType(value == null ? null : value.getClass());
        try {
            return ZLinkEncodedPayload.from(mapper.writeValueAsBytes(value));
        } catch (JsonProcessingException ex) {
            throw new IllegalArgumentException(
                "failed to serialize message as JSON: " + valueTypeName(value),
                ex);
        }
    }

    @Override
    public <T> T deserialize(ZLinkEncodedPayload payload, Class<T> type) {
        rejectRawPayloadType(type);
        try {
            return mapper.readValue(payload.bytes(), type);
        } catch (IOException ex) {
            throw new IllegalArgumentException(
                "failed to deserialize JSON message as " + type.getName(),
                ex);
        }
    }

    @Override
    public void prepare(Class<?> type) {
        if (type == null || type == Void.class) {
            return;
        }
        rejectRawPayloadType(type);
        mapper.canSerialize(type);
        mapper.canDeserialize(mapper.constructType(type));
    }

    private static void rejectRawPayloadType(Class<?> type) {
        if (type == Message.class || type == byte[].class) {
            throw new IllegalArgumentException(
                "binding Message and byte[] are not framework business payload types; use a DTO, ZLinkMessage, or ZLinkEncodedPayload");
        }
    }

    private static String valueTypeName(Object value) {
        return value == null ? "null" : value.getClass().getName();
    }
}

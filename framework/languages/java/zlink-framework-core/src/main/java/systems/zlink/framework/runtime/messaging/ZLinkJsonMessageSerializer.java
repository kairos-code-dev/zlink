package systems.zlink.framework.runtime.messaging;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
import java.io.IOException;
import systems.zlink.contracts.messaging.Message;
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
    public <T> Message serialize(T value) {
        if (value instanceof Message message) {
            return Message.from(message);
        }
        if (value instanceof byte[] bytes) {
            return Message.from(bytes);
        }
        try {
            return Message.from(mapper.writeValueAsBytes(value));
        } catch (JsonProcessingException ex) {
            throw new IllegalArgumentException(
                "failed to serialize message as JSON: " + valueTypeName(value),
                ex);
        }
    }

    @Override
    public <T> T deserialize(Message message, Class<T> type) {
        if (type == Message.class) {
            return type.cast(Message.from(message));
        }
        if (type == byte[].class) {
            return type.cast(message.toByteArray());
        }
        try {
            return mapper.readValue(message.toByteArray(), type);
        } catch (IOException ex) {
            throw new IllegalArgumentException(
                "failed to deserialize JSON message as " + type.getName(),
                ex);
        }
    }

    @Override
    public void prepare(Class<?> type) {
        if (type == null || type == Void.class || type == Message.class || type == byte[].class) {
            return;
        }
        mapper.canSerialize(type);
        mapper.canDeserialize(mapper.constructType(type));
    }

    private static String valueTypeName(Object value) {
        return value == null ? "null" : value.getClass().getName();
    }
}

package systems.zlink.framework.runtime.messaging;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.JsonGenerator;
import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.databind.DeserializationContext;
import com.fasterxml.jackson.databind.JsonDeserializer;
import com.fasterxml.jackson.databind.JsonSerializer;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializerProvider;
import com.fasterxml.jackson.databind.json.JsonMapper;
import com.fasterxml.jackson.databind.module.SimpleModule;
import java.io.IOException;
import systems.zlink.contracts.core.RoutingId;
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
            .addModule(routingIdModule())
            .build());
    }

    ZLinkJsonMessageSerializer(ObjectMapper mapper) {
        this.mapper = mapper;
    }

    @Override
    public <T> ZLinkEncodedPayload serialize(T value) {
        if (value instanceof Message message) {
            return ZLinkEncodedPayload.from(message.toByteArray());
        }
        if (value instanceof byte[] bytes) {
            return ZLinkEncodedPayload.from(bytes);
        }
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
        if (type == Message.class) {
            return type.cast(Message.from(payload.bytes()));
        }
        if (type == byte[].class) {
            return type.cast(payload.bytes());
        }
        try {
            return mapper.readValue(payload.bytes(), type);
        } catch (IOException ex) {
            throw new IllegalArgumentException(
                "failed to deserialize JSON message as "
                    + type.getName()
                    + " payload="
                    + new String(payload.bytes(), java.nio.charset.StandardCharsets.UTF_8),
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

    private static SimpleModule routingIdModule() {
        SimpleModule module = new SimpleModule("zlink-routing-id");
        module.addSerializer(RoutingId.class, new JsonSerializer<>() {
            @Override
            public void serialize(
                RoutingId value,
                JsonGenerator generator,
                SerializerProvider serializers) throws IOException {
                generator.writeString(value.toHex());
            }
        });
        module.addDeserializer(RoutingId.class, new JsonDeserializer<>() {
            @Override
            public RoutingId deserialize(
                JsonParser parser,
                DeserializationContext context) throws IOException {
                return RoutingId.fromHex(parser.getValueAsString());
            }
        });
        return module;
    }
}

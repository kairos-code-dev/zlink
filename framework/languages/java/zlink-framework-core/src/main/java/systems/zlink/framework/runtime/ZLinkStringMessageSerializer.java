package systems.zlink.framework.runtime;

import java.nio.charset.StandardCharsets;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkMessageSerializer;

public final class ZLinkStringMessageSerializer implements ZLinkMessageSerializer {
    @Override
    public <T> Message serialize(T value) {
        if (value instanceof Message message) {
            return Message.from(message);
        }
        if (value instanceof byte[] bytes) {
            return Message.from(bytes);
        }
        return Message.from(String.valueOf(value).getBytes(StandardCharsets.UTF_8));
    }

    @Override
    public <T> T deserialize(Message message, Class<T> type) {
        if (type == Message.class) {
            return type.cast(Message.from(message));
        }
        if (type == byte[].class) {
            return type.cast(message.toByteArray());
        }
        if (type == String.class) {
            return type.cast(message.toUtf8String());
        }
        throw new IllegalArgumentException("unsupported message type: " + type.getName());
    }
}

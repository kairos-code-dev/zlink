package systems.zlink.framework;

import systems.zlink.contracts.messaging.Message;

/**
 * Payload serializer used by framework handlers and connector helpers.
 */
public interface ZLinkMessageSerializer {
    <T> Message serialize(T value);

    <T> T deserialize(Message message, Class<T> type);

    default void prepare(Class<?> type) {
    }
}

package systems.zlink.framework.runtime.messaging;

import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protobuf.MessageLite;
import com.google.protobuf.Parser;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkMessageSerializer;

public final class ZLinkProtobufMessageSerializer implements ZLinkMessageSerializer {
    private final ZLinkMessageSerializer fallback;

    public ZLinkProtobufMessageSerializer(ZLinkMessageSerializer fallback) {
        this.fallback = fallback;
    }

    @Override
    public <T> Message serialize(T value) {
        if (value instanceof MessageLite protobuf) {
            return Message.from(protobuf.toByteArray());
        }
        return fallback.serialize(value);
    }

    @Override
    public <T> T deserialize(Message message, Class<T> type) {
        if (MessageLite.class.isAssignableFrom(type)) {
            return parseProtobuf(message, type);
        }
        return fallback.deserialize(message, type);
    }

    @Override
    public void prepare(Class<?> type) {
        if (type == null || MessageLite.class.isAssignableFrom(type)) {
            return;
        }
        fallback.prepare(type);
    }

    private static <T> T parseProtobuf(Message message, Class<T> type) {
        try {
            Parser<?> parser = parserFor(type);
            return type.cast(parser.parseFrom(message.toByteArray()));
        } catch (InvalidProtocolBufferException ex) {
            throw new IllegalArgumentException(
                "failed to deserialize Protobuf message as " + type.getName(),
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
}

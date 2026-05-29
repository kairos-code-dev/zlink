package systems.zlink.codec.protobuf;

import systems.zlink.contracts.messaging.Message;
import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protobuf.Parser;
import java.util.Objects;

final class ProtoCodec<T extends com.google.protobuf.Message>
    implements MessageCodec<T> {
    private final Parser<T> parser;

    public ProtoCodec(Parser<T> parser) {
        this.parser = Objects.requireNonNull(parser, "parser");
    }

    @Override
    public systems.zlink.contracts.messaging.Message toMessage(T value) {
        Objects.requireNonNull(value, "value");
        return systems.zlink.contracts.messaging.Message.from(value.toByteArray());
    }

    @Override
    public T fromMessage(systems.zlink.contracts.messaging.Message message) {
        Objects.requireNonNull(message, "message");
        try {
            return parser.parseFrom(message.dataBuffer());
        } catch (InvalidProtocolBufferException ex) {
            throw new IllegalArgumentException(
                "failed to decode protobuf payload", ex);
        }
    }
}

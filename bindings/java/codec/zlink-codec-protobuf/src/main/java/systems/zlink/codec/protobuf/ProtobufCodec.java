package systems.zlink.codec.protobuf;

import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protobuf.MessageLite;
import com.google.protobuf.Parser;
import java.util.Objects;

public final class ProtobufCodec {
    private ProtobufCodec() {}

    public static <T extends MessageLite> T parseProto(
        systems.zlink.Message message,
        Parser<T> parser) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(parser, "parser");
        try {
            return parser.parseFrom(message.toByteArray());
        } catch (InvalidProtocolBufferException ex) {
            throw new IllegalArgumentException(
                "failed to decode protobuf payload", ex);
        }
    }

    public static systems.zlink.Message toMessage(MessageLite value) {
        Objects.requireNonNull(value, "value");
        return systems.zlink.Message.copyOf(value.toByteArray());
    }
}

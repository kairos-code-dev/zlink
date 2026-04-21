package dev.kairoscode.zlink.codec.protobuf;

import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protobuf.MessageLite;
import com.google.protobuf.Parser;
import java.util.Objects;

public final class ProtobufCodec {
    private ProtobufCodec() {}

    public static <T extends MessageLite> T parseProto(
        dev.kairoscode.zlink.Message message,
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

    public static dev.kairoscode.zlink.Message toMessage(MessageLite value) {
        Objects.requireNonNull(value, "value");
        return dev.kairoscode.zlink.Message.copyOf(value.toByteArray());
    }
}

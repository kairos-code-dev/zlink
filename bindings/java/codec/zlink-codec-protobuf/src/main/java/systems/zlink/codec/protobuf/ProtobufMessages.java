package systems.zlink.codec.protobuf;

import com.google.protobuf.CodedOutputStream;
import com.google.protobuf.MessageLite;
import java.io.IOException;
import java.util.Objects;

final class ProtobufMessages {
    private ProtobufMessages() {}

    static systems.zlink.contracts.messaging.Message toMessage(MessageLite value) {
        Objects.requireNonNull(value, "value");
        systems.zlink.contracts.messaging.Message message =
            systems.zlink.contracts.messaging.Message.allocate(
                value.getSerializedSize());
        try {
            CodedOutputStream output =
                CodedOutputStream.newInstance(message.mutableDataBuffer());
            value.writeTo(output);
            output.flush();
            return message;
        } catch (IOException ex) {
            try {
                message.close();
            } catch (RuntimeException ignored) {
            }
            throw new IllegalArgumentException(
                "failed to encode protobuf payload", ex);
        }
    }
}

package systems.zlink.codec.protobuf;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import com.google.protobuf.StringValue;
import com.google.protobuf.Int32Value;
import systems.zlink.contracts.Message;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class ProtoCodecTest {
    @Test
    void stringRoundtrip() {
        StringValue original = StringValue.of("hello-zlink");
        try (Message msg = ProtobufCodec.toMessage(original)) {
            StringValue result = ProtobufCodec.parseProto(msg, StringValue.parser());
            assertEquals(original, result);
        }
    }

    @Test
    void intRoundtrip() {
        Int32Value original = Int32Value.of(42);
        try (Message msg = ProtobufCodec.toMessage(original)) {
            Int32Value result = ProtobufCodec.parseProto(msg, Int32Value.parser());
            assertEquals(original, result);
        }
    }
}

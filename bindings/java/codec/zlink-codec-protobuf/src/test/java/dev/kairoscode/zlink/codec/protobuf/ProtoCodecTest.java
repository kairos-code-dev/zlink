package dev.kairoscode.zlink.codec.protobuf;

import com.google.protobuf.StringValue;
import com.google.protobuf.Int32Value;
import dev.kairoscode.zlink.Message;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class ProtoCodecTest {
    private final ProtoCodec<StringValue> stringCodec =
        new ProtoCodec<>(StringValue.parser());
    private final ProtoCodec<Int32Value> intCodec =
        new ProtoCodec<>(Int32Value.parser());

    @Test
    void stringRoundtrip() {
        StringValue original = StringValue.of("hello-zlink");
        Message msg = stringCodec.toMessage(original);
        StringValue result = stringCodec.fromMessage(msg);
        assertEquals(original, result);
    }

    @Test
    void intRoundtrip() {
        Int32Value original = Int32Value.of(42);
        Message msg = intCodec.toMessage(original);
        Int32Value result = intCodec.fromMessage(msg);
        assertEquals(original, result);
    }
}

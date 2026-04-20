package dev.kairoscode.zlink.codec.json;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import dev.kairoscode.zlink.Message;
import org.junit.jupiter.api.Test;

class JsonCodecTest {
    private final JsonCodec<SampleValue> sampleCodec = new JsonCodec<>();
    private final JsonCodec<EmptyValue> emptyCodec = new JsonCodec<>();

    @Test
    void roundtrip() {
        SampleValue value = new SampleValue("alpha", 7, true);

        try (Message message = sampleCodec.toMessage(value)) {
            assertArrayEquals(
                "{\"name\":\"alpha\",\"count\":7,\"active\":true}".getBytes(),
                message.toByteArray());
            assertEquals(value, sampleCodec.fromMessage(message, SampleValue.class));
        }
    }

    @Test
    void nullValue() {
        assertThrows(NullPointerException.class, () -> sampleCodec.toMessage(null));
    }

    @Test
    void emptyObject() {
        EmptyValue value = new EmptyValue();

        try (Message message = emptyCodec.toMessage(value)) {
            assertEquals("{}", message.toUtf8String());
        }
    }

    record SampleValue(String name, int count, boolean active) {
    }

    @com.fasterxml.jackson.annotation.JsonAutoDetect(fieldVisibility = com.fasterxml.jackson.annotation.JsonAutoDetect.Visibility.ANY)
    static final class EmptyValue {
        public EmptyValue() {
        }

        @Override
        public boolean equals(Object obj) {
            return obj instanceof EmptyValue;
        }

        @Override
        public int hashCode() {
            return EmptyValue.class.hashCode();
        }
    }
}

package systems.zlink.codec.json;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import systems.zlink.contracts.Message;
import org.junit.jupiter.api.Test;

class JsonCodecTest {
    @Test
    void roundtrip() {
        SampleValue value = new SampleValue("alpha", 7, true);

        try (Message message = JsonCodec.toMessage(value)) {
            assertArrayEquals(
                "{\"name\":\"alpha\",\"count\":7,\"active\":true}".getBytes(),
                message.toByteArray());
            assertEquals(value, JsonCodec.parseJson(message, SampleValue.class));
        }
    }

    @Test
    void nullValue() {
        assertThrows(NullPointerException.class, () -> JsonCodec.toMessage(null));
    }

    @Test
    void emptyObject() {
        EmptyValue value = new EmptyValue();

        try (Message message = JsonCodec.toMessage(value)) {
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

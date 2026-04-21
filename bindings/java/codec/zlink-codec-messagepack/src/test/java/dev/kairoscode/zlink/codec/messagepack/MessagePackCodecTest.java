package dev.kairoscode.zlink.codec.messagepack;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import dev.kairoscode.zlink.Message;
import org.junit.jupiter.api.Test;

class MessagePackCodecTest {
    @Test
    void roundtrip() {
        SampleValue value = new SampleValue("alpha", 7, true);

        try (Message message = MessagePackCodec.toMessage(value)) {
            assertArrayEquals(new byte[] {
                (byte) 0x83,
                (byte) 0xa4, 0x6e, 0x61, 0x6d, 0x65,
                (byte) 0xa5, 0x61, 0x6c, 0x70, 0x68, 0x61,
                (byte) 0xa5, 0x63, 0x6f, 0x75, 0x6e, 0x74,
                0x07,
                (byte) 0xa6, 0x61, 0x63, 0x74, 0x69, 0x76, 0x65,
                (byte) 0xc3
            }, message.toByteArray());
            assertEquals(value,
                MessagePackCodec.parseMessagePack(message, SampleValue.class));
        }
    }

    @Test
    void nullValue() {
        assertThrows(NullPointerException.class,
            () -> MessagePackCodec.toMessage(null));
    }

    record SampleValue(String name, int count, boolean active) {
    }
}

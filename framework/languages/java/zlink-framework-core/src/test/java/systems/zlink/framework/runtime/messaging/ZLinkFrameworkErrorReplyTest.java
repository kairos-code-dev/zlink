package systems.zlink.framework.runtime.messaging;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;

final class ZLinkFrameworkErrorReplyTest {
    @Test
    void ownsFrameworkErrorReplyEncodingAndDetection() {
        List<Message> parts = ZLinkFrameworkErrorReply.create("route failed");
        try {
            assertTrue(ZLinkFrameworkErrorReply.isReply(parts));
            assertTrue(ZLinkFrameworkErrorReply.isPacketName(parts.get(0).toUtf8String()));
            assertEquals("route failed", ZLinkFrameworkErrorReply.message(parts));
        } finally {
            parts.forEach(Message::close);
        }
    }

    @Test
    void rejectsIncompleteReply() {
        try (Message marker = Message.from("ZLinkFrameworkError".getBytes(
            java.nio.charset.StandardCharsets.UTF_8))) {
            assertFalse(ZLinkFrameworkErrorReply.isReply(List.of(marker)));
        }
    }
}

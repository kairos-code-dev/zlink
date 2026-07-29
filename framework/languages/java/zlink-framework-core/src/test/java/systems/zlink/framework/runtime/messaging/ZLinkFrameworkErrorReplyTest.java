package systems.zlink.framework.runtime.messaging;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;

final class ZLinkFrameworkErrorReplyTest {
    @Test
    void ownsFrameworkErrorReplyEncodingAndDetection() {
        List<Message> parts = ZLinkFrameworkErrorReply.create("route failed");
        try {
            assertTrue(ZLinkFrameworkErrorReply.isReply(parts));
            assertTrue(ZLinkFrameworkErrorReply.isPacketName(parts.get(0).toUtf8String()));
            assertEquals("route failed", ZLinkFrameworkErrorReply.message(parts));
            assertEquals(
                ZLinkFrameworkErrorKind.REQUEST_FAILED,
                ZLinkFrameworkErrorReply.kind(parts));
        } finally {
            parts.forEach(Message::close);
        }
    }

    @Test
    void preservesTypedRequestRejection() {
        List<Message> parts = ZLinkFrameworkErrorReply.create(
            ZLinkFrameworkErrorKind.REQUEST_REJECTED,
            "filter rejected");
        try {
            assertEquals(
                ZLinkFrameworkErrorKind.REQUEST_REJECTED,
                ZLinkFrameworkErrorReply.kind(parts));
            assertEquals(
                "filter rejected",
                ZLinkFrameworkErrorReply.message(parts));
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

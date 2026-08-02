package systems.zlink.framework.runtime.spots;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Optional;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.runtime.channels.ZLinkChannelContentTypeFrame;
import systems.zlink.framework.runtime.messaging.ZLinkStringMessageSerializer;

final class ZLinkSpotRouteMessagesTest {
    private final ZLinkSpotRouteMessages messages =
        new ZLinkSpotRouteMessages(new ZLinkStringMessageSerializer());

    @Test
    void encodesPacketHeaderWithoutCopyingPayload() {
        try (Message payload = message("payload")) {
            List<Message> parts = messages.encode(Optional.of("packet"), payload);
            try {
                assertEquals("packet", parts.get(0).toUtf8String());
                assertSame(payload, parts.get(1));
            } finally {
                parts.get(0).close();
            }
        }
    }

    @Test
    void decodesReplyAndFrameworkError() {
        assertEquals(
            "reply",
            messages.decodeReply(List.of(message("reply")), String.class));

        assertThrows(
            ZLinkFrameworkException.class,
            () -> messages.decodeReply(
                List.of(
                    message("ZLinkFrameworkError"),
                    message("failed")),
                String.class));
    }

    @Test
    void encodesExplicitContentTypeForRouteTransport() {
        try (Message payload = message("payload")) {
            List<Message> parts = messages.encode(
                Optional.of("packet"), payload, "application/example");
            try {
                assertEquals(
                    "application/example",
                    ZLinkChannelContentTypeFrame.decode(parts));
            } finally {
                parts.get(0).close();
                parts.get(2).close();
            }
        }
    }

    private static Message message(String value) {
        return Message.from(value.getBytes(StandardCharsets.UTF_8));
    }
}

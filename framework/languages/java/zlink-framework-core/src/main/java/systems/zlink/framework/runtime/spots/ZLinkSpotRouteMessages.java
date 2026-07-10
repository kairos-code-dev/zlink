package systems.zlink.framework.runtime.spots;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.runtime.messaging.ZLinkMessagePayloads;

final class ZLinkSpotRouteMessages {
    private static final String FRAMEWORK_ERROR_REPLY_MARKER = "ZLinkFrameworkError";

    private final ZLinkMessageSerializer serializer;

    ZLinkSpotRouteMessages(ZLinkMessageSerializer serializer) {
        this.serializer = serializer;
    }

    List<Message> encode(Optional<String> packetName, Message payload) {
        return packetName
            .map(name -> List.of(Message.from(name.getBytes(StandardCharsets.UTF_8)), payload))
            .orElseGet(() -> List.of(payload));
    }

    <TReply> TReply decodeReply(List<Message> replyParts, Class<TReply> replyType) {
        Message emptyReply = null;
        try {
            if (isFrameworkErrorReply(replyParts)) {
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.REQUEST_FAILED,
                    replyParts.get(1).toUtf8String());
            }
            Message firstReply = replyParts.isEmpty()
                ? (emptyReply = Message.from(new byte[0]))
                : replyParts.get(replyParts.size() - 1);
            try {
                return ZLinkMessagePayloads.deserialize(serializer, firstReply, replyType);
            } catch (IllegalArgumentException ex) {
                throw new IllegalArgumentException(
                    ex.getMessage()
                        + " (spot route reply parts="
                        + describe(replyParts)
                        + ")",
                    ex);
            }
        } finally {
            if (emptyReply != null) {
                emptyReply.close();
            }
            replyParts.forEach(Message::close);
        }
    }

    private static boolean isFrameworkErrorReply(List<Message> parts) {
        return parts.size() >= 2
            && FRAMEWORK_ERROR_REPLY_MARKER.equals(parts.get(0).toUtf8String());
    }

    private static String describe(List<Message> parts) {
        List<String> descriptions = new ArrayList<>(parts.size());
        for (Message part : parts) {
            byte[] bytes = part.toByteArray();
            String text = new String(
                bytes,
                0,
                Math.min(bytes.length, 64),
                StandardCharsets.UTF_8)
                .replace("\n", "\\n")
                .replace("\r", "\\r");
            descriptions.add(bytes.length + ":" + text);
        }
        return descriptions.toString();
    }
}

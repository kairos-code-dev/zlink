package systems.zlink.framework.runtime.spots;

import java.nio.charset.StandardCharsets;
import java.util.Optional;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorReceived;
import systems.zlink.framework.runtime.streams.ZLinkStreamFrameCodec;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodec;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

final class ActorPacketFrames {
    private ActorPacketFrames() {
    }

    static Header decode(ZLinkBackendActorReceived headerPart) {
        byte[] bytes = headerPart.message().toByteArray();
        try {
            return decodeStream(bytes, headerPart.requestSeq());
        } catch (RuntimeException ignored) {
            return new Header(
                headerPart.message().toUtf8String(),
                headerPart.requestSeq(),
                false,
                0);
        }
    }

    static Message encodeReply(Header packetHeader, Message payload) {
        if (!packetHeader.streamHeader() || packetHeader.requestSeq().isEmpty()) {
            return Message.from(payload);
        }
        return Message.from(ZLinkStreamFrameCodec.encode(
            ZLinkStreamMessageKind.RESPONSE,
            ZLinkStreamCodec.fromValue(packetHeader.codec()),
            packetHeader.requestSeq(),
            packetHeader.packetName(),
            payload.toByteArray()));
    }

    static Message encodeError(Header packetHeader, Throwable error) {
        byte[] body = errorMessage(error).getBytes(StandardCharsets.UTF_8);
        if (!packetHeader.streamHeader() || packetHeader.requestSeq().isEmpty()) {
            return Message.from(body);
        }
        return Message.from(ZLinkStreamFrameCodec.encode(
            ZLinkStreamMessageKind.ERROR,
            ZLinkStreamCodec.JSON,
            packetHeader.requestSeq(),
            packetHeader.packetName(),
            body));
    }

    private static Header decodeStream(byte[] bytes, Optional<Long> backendRequestSeq) {
        ZLinkStreamHeader header = ZLinkStreamHeaderCodec.decodeOrPlain(bytes);
        if (header.kind() != ZLinkStreamMessageKind.SEND
            && header.kind() != ZLinkStreamMessageKind.REQUEST) {
            throw new IllegalArgumentException("actor STREAM header is not dispatch kind");
        }
        Optional<Long> requestSeq = header.requestSequence().isPresent()
            ? header.requestSequence()
            : backendRequestSeq;
        return new Header(
            header.packetName(),
            requestSeq,
            true,
            header.codec().value());
    }

    private static String errorMessage(Throwable error) {
        if (error == null) {
            return "handler failed";
        }
        String message = error.getMessage();
        return message == null || message.isBlank()
            ? error.getClass().getSimpleName()
            : message;
    }

    record Header(
        String packetName,
        Optional<Long> requestSeq,
        boolean streamHeader,
        int codec) {
    }
}

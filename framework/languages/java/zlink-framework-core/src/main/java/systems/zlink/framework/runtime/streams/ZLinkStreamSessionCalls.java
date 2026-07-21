package systems.zlink.framework.runtime.streams;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.backend.ZLinkBackendStreamSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdmissionKey;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.runtime.messaging.ZLinkSubmitResults;
import systems.zlink.framework.streams.ZLinkSessionClient;
import systems.zlink.framework.streams.ZLinkSessionReplyCall;
import systems.zlink.framework.streams.ZLinkSessionSendCall;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamCompressionCodec;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

final class ZLinkStreamSessionClient implements ZLinkSessionClient {
    private final ZLinkBackendStreamSocket stream;
    private final RoutingId routingId;
    private final ZLinkStreamSessionContextState context;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkStreamCodec defaultCodec;
    private final ZLinkStreamCompressionCodec compressionCodec;

    ZLinkStreamSessionClient(
        ZLinkBackendStreamSocket stream,
        RoutingId routingId,
        ZLinkStreamSessionContextState context,
        ZLinkMessageSerializer serializer,
        ZLinkStreamCodec defaultCodec,
        ZLinkStreamCompressionCodec compressionCodec) {
        this.stream = stream;
        this.routingId = routingId;
        this.context = context;
        this.serializer = serializer;
        this.defaultCodec = defaultCodec;
        this.compressionCodec = compressionCodec;
    }

    @Override
    public ZLinkSessionSendCall send(Object message) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return new ZLinkStreamSessionSendCall(
            stream,
            routingId,
            encoded.payload(),
            encoded.packetName(),
            Map.of(),
            false,
            defaultCodec,
            compressionCodec);
    }

    @Override
    public ZLinkSessionReplyCall reply(Object message) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return new ZLinkStreamSessionReplyCall(
            stream,
            routingId,
            encoded.payload(),
            context,
            encoded.packetName(),
            false,
            compressionCodec);
    }
}

record ZLinkStreamSessionSendCall(
    ZLinkBackendStreamSocket stream,
    RoutingId routingId,
    Message payload,
    String packetName,
    Map<String, String> metadata,
    boolean compressed,
    ZLinkStreamCodec codec,
    ZLinkStreamCompressionCodec compressionCodec,
    systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate submitGate)
    implements ZLinkSessionSendCall {
    ZLinkStreamSessionSendCall(
        ZLinkBackendStreamSocket stream,
        RoutingId routingId,
        Message payload,
        String packetName,
        Map<String, String> metadata,
        boolean compressed,
        ZLinkStreamCodec codec,
        ZLinkStreamCompressionCodec compressionCodec) {
        this(stream, routingId, payload, packetName, metadata, compressed, codec,
            compressionCodec,
            new systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate());
    }
    @Override
    public ZLinkSessionSendCall metadata(String key, String value) {
        Map<String, String> next = new HashMap<>(metadata);
        next.put(key, value);
        return new ZLinkStreamSessionSendCall(
            stream,
            routingId,
            payload,
            packetName,
            Map.copyOf(next),
            compressed,
            codec,
            compressionCodec);
    }

    public ZLinkSessionSendCall packetName(String messageName) {
        if (messageName == null || messageName.isBlank()) {
            throw new IllegalArgumentException("messageName is required");
        }
        return new ZLinkStreamSessionSendCall(
            stream,
            routingId,
            payload,
            messageName,
            metadata,
            compressed,
            codec,
            compressionCodec);
    }

    @Override
    public ZLinkSessionSendCall compress() {
        return new ZLinkStreamSessionSendCall(
            stream,
            routingId,
            payload,
            packetName,
            metadata,
            true,
            codec,
            compressionCodec);
    }

    @Override
    public CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> submit() {
        CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> duplicate =
            submitGate.begin();
        if (duplicate != null) {
            return duplicate;
        }
        ZLinkStreamPayloadCodec.Encoded encoded = ZLinkStreamPayloadCodec.encode(
            payload,
            compressed,
            compressionCodec);
        ZLinkStreamHeader header = new ZLinkStreamHeader(
            ZLinkStreamMessageKind.SEND,
            codec,
            encoded.flags(),
            Optional.empty(),
            packetName,
            metadata,
            Optional.of(ZLinkStreamCorrelation.next()));
        List<Message> parts = List.of(Message.from(encoded.payload()));
        return ZLinkSubmitResults.submitAsync(
            stream,
            ZLinkBackendAdmissionKey.socket(),
            () -> stream.send(routingId, header, parts, SendFlags.DONT_WAIT),
            () -> {
                parts.forEach(Message::close);
                payload.close();
            });
    }
}

record ZLinkStreamSessionReplyCall(
    ZLinkBackendStreamSocket stream,
    RoutingId routingId,
    Message payload,
    ZLinkStreamSessionContextState context,
    String packetName,
    boolean compressed,
    ZLinkStreamCompressionCodec compressionCodec,
    systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate submitGate)
    implements ZLinkSessionReplyCall {
    ZLinkStreamSessionReplyCall(
        ZLinkBackendStreamSocket stream,
        RoutingId routingId,
        Message payload,
        ZLinkStreamSessionContextState context,
        String packetName,
        boolean compressed,
        ZLinkStreamCompressionCodec compressionCodec) {
        this(stream, routingId, payload, context, packetName, compressed, compressionCodec,
            new systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate());
    }
    @Override
    public ZLinkSessionReplyCall compress() {
        return new ZLinkStreamSessionReplyCall(
            stream,
            routingId,
            payload,
            context,
            packetName,
            true,
            compressionCodec);
    }

    @Override
    public CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> submit() {
        Optional<ZLinkStreamHeader> currentHeader = context.currentDispatchHeader();
        if (currentHeader.isEmpty() || currentHeader.get().requestSequence().isEmpty()) {
            CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> duplicate =
                submitGate.begin();
            if (duplicate != null) {
                return duplicate;
            }
            payload.close();
            return CompletableFuture.failedFuture(new IllegalStateException(
                "Reply is only available while handling a request packet."));
        }
        CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> duplicate =
            submitGate.begin();
        if (duplicate != null) {
            return duplicate;
        }
        if (!context.claimReplyHeader(currentHeader.get())) {
            payload.close();
            return CompletableFuture.failedFuture(
                new IllegalStateException("The request reply token has already been used."));
        }
        ZLinkStreamPayloadCodec.Encoded encoded = ZLinkStreamPayloadCodec.encode(
            payload,
            compressed,
            compressionCodec);
        List<Message> parts = List.of(Message.from(encoded.payload()));
        ZLinkStreamHeader current = currentHeader.get();
        ZLinkStreamHeader replyHeader = ZLinkStreamHeader.createResponse(
            current,
            current.codec(),
            encoded.flags(),
            packetName,
            Map.of());
        return ZLinkSubmitResults.submitAsync(
            stream,
            ZLinkBackendAdmissionKey.socket(),
            () -> {
                boolean accepted = stream.reply(
                    routingId, replyHeader, parts, SendFlags.DONT_WAIT);
                if (accepted) {
                    context.traceStreamReplied(current);
                }
                return accepted;
            },
            () -> {
                parts.forEach(Message::close);
                payload.close();
            });
    }
}

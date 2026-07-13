package systems.zlink.stream.connector;

import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletionStage;
import java.util.function.Consumer;
import java.util.function.Function;
import systems.zlink.contracts.messaging.Message;

final class ZLinkStreamReceiveDispatcher {
    private static final String HEARTBEAT_PING_NAME = "$zlink.heartbeat.ping";
    private static final String HEARTBEAT_PONG_NAME = "$zlink.heartbeat.pong";

    private final ZLinkStreamConnectorConfiguration configuration;
    private final Map<String, List<ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload>>> handlers;
    private final ZLinkStreamDispatchQueue dispatchQueue;
    private final ZLinkStreamPendingRequests pendingRequests;
    private final ZLinkStreamInboundObserverDispatcher inboundObservers;
    private final ZLinkStreamConnectorPayloadCodec payloadCodec;
    private final Consumer<ZLinkStreamError> errorPublisher;
    private final Function<String, CompletionStage<Void>> controlSender;
    private final Consumer<ZLinkStreamCloseReason> closeReasonReceived;

    ZLinkStreamReceiveDispatcher(
        ZLinkStreamConnectorConfiguration configuration,
        Map<String, List<ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload>>> handlers,
        ZLinkStreamDispatchQueue dispatchQueue,
        ZLinkStreamPendingRequests pendingRequests,
        ZLinkStreamInboundObserverDispatcher inboundObservers,
        ZLinkStreamConnectorPayloadCodec payloadCodec,
        Consumer<ZLinkStreamError> errorPublisher,
        Function<String, CompletionStage<Void>> controlSender,
        Consumer<ZLinkStreamCloseReason> closeReasonReceived) {
        this.configuration = configuration;
        this.handlers = handlers;
        this.dispatchQueue = dispatchQueue;
        this.pendingRequests = pendingRequests;
        this.inboundObservers = inboundObservers;
        this.payloadCodec = payloadCodec;
        this.errorPublisher = errorPublisher;
        this.controlSender = controlSender;
        this.closeReasonReceived = closeReasonReceived;
    }

    void dispatch(byte[] encodedHeader, byte[] payload) {
        ZLinkStreamWireProtocol.Header header = ZLinkStreamWireProtocol.decodeHeader(encodedHeader);
        byte[] decodedPayload = payloadCodec.decode(header, payload);
        DefaultZLinkStreamConnector.trace("connector read-frame endpoint=" + configuration.endpoint()
            + " kind=" + header.kind()
            + " name=" + header.name()
            + " requestSeq=" + header.requestSeq()
            + " bytes=" + decodedPayload.length
            + " correlation=" + header.correlationId()
            + " flow=" + header.flowId()
            + " origin=" + flowOriginName(header.flowOrigin()));
        inboundObservers.enqueue(header, payload);
        if (header.kind() == ZLinkStreamWireProtocol.KIND_CONTROL) {
            dispatchControl(header, decodedPayload);
            return;
        }
        if (header.kind() == ZLinkStreamWireProtocol.KIND_RESPONSE) {
            completeResponse(header, decodedPayload);
            return;
        }
        if (header.kind() == ZLinkStreamWireProtocol.KIND_ERROR) {
            dispatchError(header, decodedPayload);
            return;
        }
        if (header.kind() == ZLinkStreamWireProtocol.KIND_SEND
            || header.kind() == ZLinkStreamWireProtocol.KIND_REQUEST) {
            dispatchToHandlers(header, decodedPayload);
        }
    }

    private static String flowOriginName(int origin) {
        return switch (origin) {
            case 1 -> "inbound";
            case 2 -> "timer";
            case 3 -> "application";
            case 4 -> "lifecycle";
            default -> null;
        };
    }

    private void dispatchControl(ZLinkStreamWireProtocol.Header header, byte[] payload) {
        if (ZLinkSessionClosingControl.NAME.equals(header.name())) {
            try {
                ZLinkStreamCloseReason reason = ZLinkSessionClosingControl.decode(payload);
                DefaultZLinkStreamConnector.trace(
                    "connector session-closing version=" + ZLinkSessionClosingControl.VERSION
                        + " reason=" + reason.name().toLowerCase());
                closeReasonReceived.accept(reason);
            } catch (IllegalArgumentException invalidControl) {
                closeReasonReceived.accept(ZLinkStreamCloseReason.PROTOCOL_ERROR);
                throw invalidControl;
            }
            return;
        }
        if (payload.length != 0) {
            closeReasonReceived.accept(ZLinkStreamCloseReason.PROTOCOL_ERROR);
            throw new IllegalArgumentException("heartbeat control packet payload must be empty");
        }
        if (HEARTBEAT_PING_NAME.equals(header.name())) {
            controlSender.apply(HEARTBEAT_PONG_NAME);
            return;
        }
        if (HEARTBEAT_PONG_NAME.equals(header.name())) {
            return;
        }
        closeReasonReceived.accept(ZLinkStreamCloseReason.PROTOCOL_ERROR);
        throw new IllegalArgumentException("unknown control packet");
    }

    private void completeResponse(ZLinkStreamWireProtocol.Header header, byte[] payload) {
        pendingRequests.complete(
            header.requestSeq(),
            new ZLinkStreamEncodedPayload(
                header.name(),
                Message.from(payload),
                header.metadata(),
                ZLinkStreamConnectorPayloadCodec.fromWireCodec(header.codec())));
    }

    private void dispatchError(ZLinkStreamWireProtocol.Header header, byte[] payload) {
        ZLinkStreamError error = new ZLinkStreamError(
            ZLinkStreamErrorCode.REMOTE_ERROR,
            new String(payload, StandardCharsets.UTF_8));
        errorPublisher.accept(error);
        if (header.requestSeq() != null) {
            pendingRequests.fail(header.requestSeq(), new IllegalStateException(error.message()));
        }
    }

    private void dispatchToHandlers(ZLinkStreamWireProtocol.Header header, byte[] payload) {
        List<ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload>> registered =
            List.copyOf(handlers.getOrDefault(header.name(), List.of()));
        if (registered.isEmpty()) {
            return;
        }
        Runnable dispatch = () -> {
            for (ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload> handler : registered) {
                invokeUserCallback(() -> handler.handleAsync(new ZLinkStreamMessage<>(
                    header.name(),
                    new ZLinkStreamEncodedPayload(
                        header.name(),
                        Message.from(payload),
                        header.metadata(),
                        ZLinkStreamConnectorPayloadCodec.fromWireCodec(header.codec())),
                    header.metadata())));
            }
        };
        if (configuration.dispatchMode() == ZLinkStreamDispatchMode.AUTO) {
            dispatch.run();
        } else {
            dispatchQueue.add(header.name(), dispatch);
        }
    }

    private void invokeUserCallback(UserCallback callback) {
        try {
            callback.invoke().exceptionally(ex -> {
                errorPublisher.accept(DefaultZLinkStreamConnector.userCallbackFailed(ex));
                return null;
            });
        } catch (Throwable ex) {
            errorPublisher.accept(DefaultZLinkStreamConnector.userCallbackFailed(ex));
        }
    }

    @FunctionalInterface
    private interface UserCallback {
        CompletionStage<Void> invoke();
    }
}

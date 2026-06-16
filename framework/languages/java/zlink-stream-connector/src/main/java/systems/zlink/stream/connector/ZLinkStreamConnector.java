package systems.zlink.stream.connector;

import java.util.Objects;
import java.util.concurrent.CompletionStage;

public interface ZLinkStreamConnector {
    boolean isConnected();

    ZLinkStreamConnectionState state();

    ZLinkStreamConnectorOptions options();

    int pendingDispatchCount();

    int receivedCount(String name);

    ZLinkStreamLifecycleCall connect();

    ZLinkStreamLifecycleCall disconnect();

    ZLinkStreamLifecycleCall reconnect();

    ZLinkStreamLifecycleCall close();

    ZLinkStreamLifecycleCall dispatch();

    default <T> T await(CompletionStage<T> stage) throws Exception {
        Objects.requireNonNull(stage, "stage");
        return ZLinkStreamCompletions.await(stage);
    }

    ZLinkStreamSendCall send(ZLinkStreamEncodedPayload payload);

    ZLinkStreamRequestCall request(ZLinkStreamEncodedPayload payload);

    AutoCloseable observeInbound(ZLinkStreamInboundObserver observer);

    default ZLinkStreamWaitCall waitFor(String name) {
        return new DefaultZLinkStreamWaitCall(
            this,
            name,
            options().requestTimeout(),
            options().typedCodec());
    }

    default ZLinkStreamWaitCall waitFor(Class<?> payloadType) {
        Objects.requireNonNull(payloadType, "payloadType");
        return waitFor(options().nameResolver().resolve(payloadType));
    }

    AutoCloseable on(
        String name,
        ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload> handler);

    default <TPayload> AutoCloseable on(
        Class<TPayload> payloadType,
        ZLinkStreamMessageHandler<TPayload> handler) {
        return on(options().nameResolver().resolve(payloadType), payloadType, handler);
    }

    default <TPayload> AutoCloseable on(
        String name,
        Class<TPayload> payloadType,
        ZLinkStreamMessageHandler<TPayload> handler) {
        Objects.requireNonNull(payloadType, "payloadType");
        Objects.requireNonNull(handler, "handler");
        ZLinkStreamTypedCodec codec = requireTypedCodec();
        return on(name, message -> handler.handleAsync(new ZLinkStreamMessage<>(
            message.packetName(),
            codec.decode(message.payload(), payloadType),
            message.metadata())));
    }

    AutoCloseable onErrorReceived(ZLinkStreamErrorHandler handler);

    AutoCloseable onDisconnected(ZLinkStreamDisconnectedHandler handler);

    AutoCloseable onConnectionStateChanged(ZLinkStreamConnectionStateHandler handler);

    private ZLinkStreamTypedCodec requireTypedCodec() {
        ZLinkStreamTypedCodec codec = options().typedCodec();
        if (codec == null) {
            throw new IllegalStateException(
                "typed stream payload API requires ZLinkStreamConnectorOptions.typedCodec");
        }
        return codec;
    }

}

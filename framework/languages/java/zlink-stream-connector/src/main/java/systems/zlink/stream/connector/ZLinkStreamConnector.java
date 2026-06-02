package systems.zlink.stream.connector;

import java.util.concurrent.CompletionStage;

public interface ZLinkStreamConnector extends AutoCloseable {
    boolean isConnected();

    ZLinkStreamConnectionState state();

    ZLinkStreamConnectorOptions options();

    int pendingDispatchCount();

    CompletionStage<Void> connectAsync();

    CompletionStage<Void> closeAsync();

    CompletionStage<Void> dispatchAsync();

    ZLinkStreamSendCall send(ZLinkStreamEncodedPayload payload);

    ZLinkStreamRequestCall request(ZLinkStreamEncodedPayload payload);

    AutoCloseable on(
        String name,
        ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload> handler);

    AutoCloseable onDisconnected(ZLinkStreamDisconnectedHandler handler);

    AutoCloseable onConnectionStateChanged(ZLinkStreamConnectionStateHandler handler);

    @Override
    void close();
}

package systems.zlink.stream.connector;

import java.time.Duration;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Queue;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeoutException;
import systems.zlink.contracts.messaging.Message;

final class DefaultZLinkStreamConnector implements ZLinkStreamConnector {
    private static final String RESERVED_PACKET_NAME_PREFIX = "__zlink.";
    private static final int MAX_PACKET_NAME_BYTES = 255;

    private final ZLinkStreamConnectorOptions options;
    private final Map<String, List<ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload>>> handlers =
        new HashMap<>();
    private final List<ZLinkStreamDisconnectedHandler> disconnectedHandlers = new ArrayList<>();
    private final List<ZLinkStreamConnectionStateHandler> stateHandlers = new ArrayList<>();
    private final Queue<Runnable> dispatchQueue = new ArrayDeque<>();
    private ZLinkStreamConnectionState state = ZLinkStreamConnectionState.DISCONNECTED;

    DefaultZLinkStreamConnector(ZLinkStreamConnectorOptions options) {
        this.options = validate(options);
    }

    @Override
    public boolean isConnected() {
        return state == ZLinkStreamConnectionState.CONNECTED;
    }

    @Override
    public ZLinkStreamConnectionState state() {
        return state;
    }

    @Override
    public ZLinkStreamConnectorOptions options() {
        return options;
    }

    @Override
    public int pendingDispatchCount() {
        return dispatchQueue.size();
    }

    @Override
    public CompletionStage<Void> connectAsync() {
        if (state == ZLinkStreamConnectionState.CLOSED) {
            throw new IllegalStateException("connector is closed");
        }
        transitionTo(ZLinkStreamConnectionState.CONNECTED);
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> disconnectAsync() {
        if (state == ZLinkStreamConnectionState.CLOSED) {
            throw new IllegalStateException("connector is closed");
        }
        boolean wasConnected = isConnected();
        transitionTo(ZLinkStreamConnectionState.DISCONNECTED);
        dispatchQueue.clear();
        if (wasConnected) {
            notifyDisconnected();
        }
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> reconnectAsync() {
        if (state == ZLinkStreamConnectionState.CLOSED) {
            throw new IllegalStateException("connector is closed");
        }
        if (isConnected()) {
            return CompletableFuture.completedFuture(null);
        }
        if (options.maxReconnectAttempts() == 0) {
            return CompletableFuture.failedFuture(
                new IllegalStateException("reconnect attempts are disabled"));
        }
        transitionTo(ZLinkStreamConnectionState.RECONNECTING);
        transitionTo(ZLinkStreamConnectionState.CONNECTED);
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> closeAsync() {
        boolean wasConnected = isConnected();
        transitionTo(ZLinkStreamConnectionState.CLOSED);
        dispatchQueue.clear();
        if (wasConnected) {
            notifyDisconnected();
        }
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> dispatchAsync() {
        while (!dispatchQueue.isEmpty()) {
            dispatchQueue.remove().run();
        }
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public ZLinkStreamSendCall send(ZLinkStreamEncodedPayload payload) {
        return new SendCall(this, copyPayload(payload));
    }

    @Override
    public ZLinkStreamRequestCall request(ZLinkStreamEncodedPayload payload) {
        return new RequestCall(this, copyPayload(payload), options.requestTimeout());
    }

    @Override
    public AutoCloseable on(
        String name,
        ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload> handler) {
        requirePacketName(name);
        Objects.requireNonNull(handler, "handler");
        handlers.computeIfAbsent(name, ignored -> new ArrayList<>()).add(handler);
        return () -> handlers.getOrDefault(name, List.of()).remove(handler);
    }

    @Override
    public AutoCloseable onDisconnected(ZLinkStreamDisconnectedHandler handler) {
        Objects.requireNonNull(handler, "handler");
        disconnectedHandlers.add(handler);
        return () -> disconnectedHandlers.remove(handler);
    }

    @Override
    public AutoCloseable onConnectionStateChanged(ZLinkStreamConnectionStateHandler handler) {
        Objects.requireNonNull(handler, "handler");
        stateHandlers.add(handler);
        return () -> stateHandlers.remove(handler);
    }

    @Override
    public void close() {
        closeAsync().toCompletableFuture().join();
    }

    private void transitionTo(ZLinkStreamConnectionState next) {
        if (state == next) {
            return;
        }
        state = next;
        for (ZLinkStreamConnectionStateHandler handler : List.copyOf(stateHandlers)) {
            handler.handleAsync(next).toCompletableFuture().join();
        }
    }

    private void submit(ZLinkStreamEncodedPayload payload) {
        ensureConnected();
        List<ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload>> registered =
            List.copyOf(handlers.getOrDefault(payload.packetName(), List.of()));
        Runnable dispatch = () -> {
            for (ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload> handler : registered) {
                handler.handleAsync(new ZLinkStreamMessage<>(
                    payload.packetName(),
                    copyPayload(payload),
                    payload.metadata())).toCompletableFuture().join();
            }
            payload.payload().close();
        };
        if (options.dispatchMode() == ZLinkStreamDispatchMode.AUTO) {
            dispatch.run();
        } else {
            dispatchQueue.add(dispatch);
        }
    }

    private CompletionStage<ZLinkStreamEncodedPayload> submitRequest(
        ZLinkStreamEncodedPayload payload,
        Duration timeout) {
        ensureConnected();
        if (!handlers.containsKey(payload.packetName())
            || handlers.get(payload.packetName()).isEmpty()) {
            payload.payload().close();
            return CompletableFuture.failedFuture(
                new TimeoutException("request timed out after " + timeout));
        }
        submit(copyPayload(payload));
        return CompletableFuture.completedFuture(copyPayload(payload));
    }

    private void ensureConnected() {
        if (!isConnected()) {
            throw new IllegalStateException("connector is not connected");
        }
    }

    private static ZLinkStreamConnectorOptions validate(
        ZLinkStreamConnectorOptions options) {
        Objects.requireNonNull(options, "options");
        Objects.requireNonNull(options.endpoint(), "endpoint");
        requireSupportedEndpointScheme(options.endpoint().getScheme());
        Objects.requireNonNull(options.dispatchMode(), "dispatchMode");
        Duration timeout = Objects.requireNonNull(
            options.requestTimeout(),
            "requestTimeout");
        if (timeout.isZero() || timeout.isNegative()) {
            throw new IllegalArgumentException("requestTimeout must be positive");
        }
        if (options.maxReconnectAttempts() < 0) {
            throw new IllegalArgumentException("maxReconnectAttempts must be >= 0");
        }
        return options;
    }

    private void notifyDisconnected() {
        for (ZLinkStreamDisconnectedHandler handler : List.copyOf(disconnectedHandlers)) {
            handler.handleAsync().toCompletableFuture().join();
        }
    }

    private static void requireSupportedEndpointScheme(String scheme) {
        if (scheme == null || scheme.isBlank()) {
            throw new IllegalArgumentException("endpoint URI scheme is required");
        }
        if (!List.of("tcp", "tls", "ws", "wss").contains(scheme)) {
            throw new IllegalArgumentException("unsupported endpoint URI scheme: " + scheme);
        }
    }

    private static ZLinkStreamEncodedPayload copyPayload(
        ZLinkStreamEncodedPayload payload) {
        Objects.requireNonNull(payload, "payload");
        return new ZLinkStreamEncodedPayload(
            requirePacketName(payload.packetName()),
            Message.from(payload.payload()),
            Map.copyOf(payload.metadata()));
    }

    private static String requirePacketName(String packetName) {
        if (packetName == null || packetName.isBlank()) {
            throw new IllegalArgumentException("packetName is required");
        }
        if (packetName.startsWith(RESERVED_PACKET_NAME_PREFIX)) {
            throw new IllegalArgumentException("packetName uses a reserved zlink prefix");
        }
        if (packetName.getBytes(StandardCharsets.UTF_8).length > MAX_PACKET_NAME_BYTES) {
            throw new IllegalArgumentException("packetName must not exceed 255 UTF-8 bytes");
        }
        return packetName;
    }

    private record SendCall(
        DefaultZLinkStreamConnector connector,
        ZLinkStreamEncodedPayload payload) implements ZLinkStreamSendCall {
        @Override
        public ZLinkStreamSendCall packetName(String packetName) {
            return new SendCall(connector, new ZLinkStreamEncodedPayload(
                requirePacketName(packetName),
                payload.payload(),
                payload.metadata()));
        }

        @Override
        public ZLinkStreamSendCall metadata(String key, String value) {
            Map<String, String> metadata = new HashMap<>(payload.metadata());
            metadata.put(key, value);
            return new SendCall(connector, new ZLinkStreamEncodedPayload(
                payload.packetName(),
                payload.payload(),
                metadata));
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            connector.submit(payload);
            return CompletableFuture.completedFuture(null);
        }
    }

    private record RequestCall(
        DefaultZLinkStreamConnector connector,
        ZLinkStreamEncodedPayload payload,
        Duration timeout) implements ZLinkStreamRequestCall {
        @Override
        public ZLinkStreamRequestCall packetName(String packetName) {
            return new RequestCall(connector, new ZLinkStreamEncodedPayload(
                requirePacketName(packetName),
                payload.payload(),
                payload.metadata()), timeout);
        }

        @Override
        public ZLinkStreamRequestCall metadata(String key, String value) {
            Map<String, String> metadata = new HashMap<>(payload.metadata());
            metadata.put(key, value);
            return new RequestCall(connector, new ZLinkStreamEncodedPayload(
                payload.packetName(),
                payload.payload(),
                metadata), timeout);
        }

        @Override
        public ZLinkStreamRequestCall timeout(Duration timeout) {
            if (timeout == null || timeout.isZero() || timeout.isNegative()) {
                throw new IllegalArgumentException("timeout must be positive");
            }
            return new RequestCall(connector, payload, timeout);
        }

        @Override
        public CompletionStage<ZLinkStreamEncodedPayload> submitAsync() {
            return connector.submitRequest(payload, timeout);
        }
    }
}

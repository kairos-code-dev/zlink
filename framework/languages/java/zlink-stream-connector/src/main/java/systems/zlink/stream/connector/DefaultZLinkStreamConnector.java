package systems.zlink.stream.connector;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.http.HttpClient;
import java.net.StandardSocketOptions;
import java.nio.charset.StandardCharsets;
import java.nio.channels.AsynchronousSocketChannel;
import java.nio.channels.CompletionHandler;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicLong;
import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;
import systems.zlink.contracts.messaging.Message;

final class DefaultZLinkStreamConnector implements ZLinkStreamConnector {
    private static final String RESERVED_PACKET_NAME_PREFIX = "$zlink.";
    private static final String HEARTBEAT_PING_NAME = "$zlink.heartbeat.ping";
    private static final String HEARTBEAT_PONG_NAME = "$zlink.heartbeat.pong";
    private static final int MAX_PACKET_NAME_BYTES = 255;
    private static final ScheduledExecutorService TIMEOUTS =
        Executors.newSingleThreadScheduledExecutor(new DaemonThreadFactory());

    private final ZLinkStreamConnectorOptions options;
    private final Map<String, List<ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload>>> handlers =
        new ConcurrentHashMap<>();
    private final List<ZLinkStreamDisconnectedHandler> disconnectedHandlers = new ArrayList<>();
    private final List<ZLinkStreamConnectionStateHandler> stateHandlers = new ArrayList<>();
    private final ZLinkStreamDispatchQueue dispatchQueue = new ZLinkStreamDispatchQueue();
    private final AtomicLong nextRequestSeq = new AtomicLong();
    private final ZLinkStreamPendingRequests pendingRequests = new ZLinkStreamPendingRequests();

    private volatile ZLinkStreamConnectionState state = ZLinkStreamConnectionState.DISCONNECTED;
    private volatile ZLinkStreamTransportConnection connection;
    private volatile ScheduledFuture<?> heartbeatTask;
    private volatile long lastInboundNanos = System.nanoTime();
    private CompletableFuture<Void> sendChain = CompletableFuture.completedFuture(null);

    DefaultZLinkStreamConnector(ZLinkStreamConnectorOptions options) {
        this.options = validate(options);
    }

    @Override
    public boolean isConnected() {
        return state == ZLinkStreamConnectionState.CONNECTED
            && connection != null
            && connection.isOpen();
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
        if (isConnected()) {
            return CompletableFuture.completedFuture(null);
        }

        return connectOnceAsync();
    }

    private CompletionStage<Void> connectOnceAsync() {
        if (isWebSocketEndpoint()) {
            return connectWebSocketAsync();
        }
        if (isTlsEndpoint()) {
            return ZLinkTlsTransportConnection.connectAsync(
                options.endpoint(),
                options.connectTimeout(),
                options.skipServerCertificateValidation()).thenAccept(this::activateConnection);
        }
        CompletableFuture<Void> result = new CompletableFuture<>();
        AsynchronousSocketChannel channel;
        try {
            channel = AsynchronousSocketChannel.open();
            channel.setOption(StandardSocketOptions.SO_KEEPALIVE, true);
        } catch (IOException ex) {
            return CompletableFuture.failedFuture(ex);
        }

        var timeout = TIMEOUTS.schedule(
            () -> result.completeExceptionally(
                new TimeoutException("connect timed out after " + options.connectTimeout())),
            options.connectTimeout().toMillis(),
            TimeUnit.MILLISECONDS);

        InetSocketAddress address = new InetSocketAddress(
            options.endpoint().getHost(),
            resolvePort(options.endpoint()));
        channel.connect(address, null, new CompletionHandler<Void, Void>() {
            @Override
            public void completed(Void ignored, Void attachment) {
                timeout.cancel(false);
                if (state == ZLinkStreamConnectionState.CLOSED) {
                    closeRawQuietly(channel);
                    result.completeExceptionally(new IllegalStateException("connector is closed"));
                    return;
                }
                ZLinkTcpTransportConnection tcp = new ZLinkTcpTransportConnection(channel);
                activateConnection(tcp);
                result.complete(null);
            }

            @Override
            public void failed(Throwable exc, Void attachment) {
                timeout.cancel(false);
                closeRawQuietly(channel);
                result.completeExceptionally(exc);
            }
        });
        result.whenComplete((ignored, ex) -> {
            if (ex != null) {
                closeRawQuietly(channel);
            }
        });
        return result;
    }

    private void activateConnection(ZLinkStreamTransportConnection transport) {
        connection = transport;
        lastInboundNanos = System.nanoTime();
        transitionTo(ZLinkStreamConnectionState.CONNECTED);
        startReceiveLoop(transport);
        startHeartbeat();
    }

    @Override
    public CompletionStage<Void> disconnectAsync() {
        if (state == ZLinkStreamConnectionState.CLOSED) {
            throw new IllegalStateException("connector is closed");
        }
        boolean wasConnected = isConnected();
        ZLinkStreamTransportConnection current = connection;
        connection = null;
        stopHeartbeat();
        closeQuietly(current);
        failPending(new IOException("connector disconnected"));
        dispatchQueue.clear();
        transitionTo(ZLinkStreamConnectionState.DISCONNECTED);
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
        if (!options.reconnectEnabled()) {
            return CompletableFuture.failedFuture(
                new IllegalStateException("reconnect attempts are disabled"));
        }
        transitionTo(ZLinkStreamConnectionState.RECONNECTING);
        return reconnectAttemptAsync(1, options.reconnectInitialDelay());
    }

    @Override
    public CompletionStage<Void> closeAsync() {
        boolean wasConnected = isConnected();
        ZLinkStreamTransportConnection current = connection;
        connection = null;
        stopHeartbeat();
        closeQuietly(current);
        failPending(new IOException("connector closed"));
        dispatchQueue.clear();
        transitionTo(ZLinkStreamConnectionState.CLOSED);
        if (wasConnected) {
            notifyDisconnected();
        }
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> dispatchAsync() {
        dispatchQueue.drain();
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public ZLinkStreamSendCall send(ZLinkStreamEncodedPayload payload) {
        return new SendCall(this, copyPayload(payload), false);
    }

    @Override
    public ZLinkStreamRequestCall request(ZLinkStreamEncodedPayload payload) {
        return new RequestCall(this, copyPayload(payload), options.requestTimeout(), false);
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
        closeAsync();
    }

    private CompletionStage<Void> submit(ZLinkStreamEncodedPayload payload, boolean compress) {
        ensureConnected();
        byte[] body = encodePayload(payload, compress);
        ZLinkStreamWireProtocol.Header header = new ZLinkStreamWireProtocol.Header(
            ZLinkStreamWireProtocol.KIND_SEND,
            toWireCodec(payload.codec()),
            (payload.metadata().isEmpty() ? 0 : ZLinkStreamWireProtocol.FLAG_HAS_METADATA)
                | (compress ? ZLinkStreamWireProtocol.FLAG_PAYLOAD_COMPRESSED : 0),
            null,
            payload.packetName(),
            payload.metadata());
        return sendFrame(header, body);
    }

    private CompletionStage<ZLinkStreamEncodedPayload> submitRequest(
        ZLinkStreamEncodedPayload payload,
        Duration timeout,
        boolean compress) {
        ensureConnected();
        long requestSeq = nextRequestSeq();
        CompletableFuture<ZLinkStreamEncodedPayload> pending =
            pendingRequests.add(requestSeq, timeout, TIMEOUTS);

        byte[] body = encodePayload(payload, compress);
        ZLinkStreamWireProtocol.Header header = new ZLinkStreamWireProtocol.Header(
            ZLinkStreamWireProtocol.KIND_REQUEST,
            toWireCodec(payload.codec()),
            ZLinkStreamWireProtocol.FLAG_HAS_REQUEST_SEQ
                | (payload.metadata().isEmpty() ? 0 : ZLinkStreamWireProtocol.FLAG_HAS_METADATA)
                | (compress ? ZLinkStreamWireProtocol.FLAG_PAYLOAD_COMPRESSED : 0),
            requestSeq,
            payload.packetName(),
            payload.metadata());

        sendFrame(header, body).whenComplete((ignored, ex) -> {
            if (ex != null) {
                pendingRequests.fail(requestSeq, ex);
            }
        });
        return pending;
    }

    private CompletionStage<Void> sendFrame(
        ZLinkStreamWireProtocol.Header header,
        byte[] payload) {
        byte[] encodedHeader = ZLinkStreamWireProtocol.encodeHeader(header);
        byte[] frame = ZLinkStreamWireProtocol.encodeFrame(
            encodedHeader,
            payload,
            options.maxSendPayloadSize());
        synchronized (this) {
            sendChain = sendChain.thenCompose(ignored -> writeFrame(frame));
            return sendChain;
        }
    }

    private CompletionStage<Void> writeFrame(byte[] frame) {
        ZLinkStreamTransportConnection current = connection;
        if (current == null) {
            return CompletableFuture.failedFuture(
                new IllegalStateException("connector is not connected"));
        }
        return current.writeAsync(frame);
    }

    private void startReceiveLoop(ZLinkStreamTransportConnection transport) {
        readNextFrame(transport);
    }

    private void readNextFrame(ZLinkStreamTransportConnection transport) {
        transport.readFrameAsync().whenComplete((frame, ex) -> {
            if (ex != null) {
                handleReceiveFailure(transport, ex);
                return;
            }
            try {
                dispatchInbound(frame.header(), frame.payload());
                if (connection == transport && state == ZLinkStreamConnectionState.CONNECTED) {
                    readNextFrame(transport);
                }
            } catch (RuntimeException dispatchEx) {
                handleReceiveFailure(transport, dispatchEx);
            }
        });
    }

    private void dispatchInbound(byte[] encodedHeader, byte[] payload) {
        ZLinkStreamWireProtocol.Header header =
            ZLinkStreamWireProtocol.decodeHeader(encodedHeader);
        lastInboundNanos = System.nanoTime();
        byte[] decodedPayload = decodePayload(header, payload);
        if (header.kind() == ZLinkStreamWireProtocol.KIND_CONTROL) {
            dispatchControl(header, decodedPayload);
            return;
        }
        if (header.kind() == ZLinkStreamWireProtocol.KIND_RESPONSE) {
            pendingRequests.complete(
                header.requestSeq(),
                new ZLinkStreamEncodedPayload(
                    header.name(),
                    Message.from(decodedPayload),
                    header.metadata(),
                    fromWireCodec(header.codec())));
            return;
        }
        if (header.kind() == ZLinkStreamWireProtocol.KIND_ERROR
            && header.requestSeq() != null) {
            pendingRequests.fail(
                header.requestSeq(),
                new IllegalStateException(new String(decodedPayload, StandardCharsets.UTF_8)));
            return;
        }
        if (header.kind() == ZLinkStreamWireProtocol.KIND_SEND
            || header.kind() == ZLinkStreamWireProtocol.KIND_REQUEST) {
            dispatchToHandlers(header, decodedPayload);
        }
    }

    private void dispatchControl(ZLinkStreamWireProtocol.Header header, byte[] payload) {
        if (payload.length != 0) {
            throw new IllegalArgumentException("control packet payload must be empty");
        }
        if (HEARTBEAT_PING_NAME.equals(header.name())) {
            sendControl(HEARTBEAT_PONG_NAME);
            return;
        }
        if (HEARTBEAT_PONG_NAME.equals(header.name())) {
            return;
        }
        throw new IllegalArgumentException("unknown control packet");
    }

    private void dispatchToHandlers(ZLinkStreamWireProtocol.Header header, byte[] payload) {
        List<ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload>> registered =
            List.copyOf(handlers.getOrDefault(header.name(), List.of()));
        if (registered.isEmpty()) {
            return;
        }
        Runnable dispatch = () -> {
            for (ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload> handler : registered) {
                handler.handleAsync(new ZLinkStreamMessage<>(
                    header.name(),
                    new ZLinkStreamEncodedPayload(
                        header.name(),
                        Message.from(payload),
                        header.metadata(),
                        fromWireCodec(header.codec())),
                    header.metadata())).exceptionally(ex -> null);
            }
        };
        if (options.dispatchMode() == ZLinkStreamDispatchMode.AUTO) {
            dispatch.run();
        } else {
            dispatchQueue.add(dispatch);
        }
    }

    private void handleReceiveFailure(ZLinkStreamTransportConnection failed, Throwable ex) {
        if (connection != failed || state != ZLinkStreamConnectionState.CONNECTED) {
            return;
        }
        connection = null;
        stopHeartbeat();
        closeQuietly(failed);
        failPending(ex);
        transitionTo(options.reconnectEnabled()
            ? ZLinkStreamConnectionState.RECONNECTING
            : ZLinkStreamConnectionState.DISCONNECTED);
        notifyDisconnected();
        if (state == ZLinkStreamConnectionState.RECONNECTING) {
            reconnectAttemptAsync(1, options.reconnectInitialDelay());
        }
    }

    private CompletionStage<Void> connectWebSocketAsync() {
        HttpClient.Builder clientBuilder = HttpClient.newBuilder()
            .connectTimeout(options.connectTimeout());
        if (options.skipServerCertificateValidation()) {
            clientBuilder.sslContext(insecureSslContext());
        }
        return ZLinkWebSocketTransportConnection.connectAsync(
            clientBuilder.build(),
            options.endpoint()).thenAccept(ws -> {
                if (state == ZLinkStreamConnectionState.CLOSED) {
                    closeQuietly(ws);
                    throw new IllegalStateException("connector is closed");
                }
                activateConnection(ws);
            });
    }

    private static SSLContext insecureSslContext() {
        try {
            TrustManager[] trustAllManagers = new TrustManager[] {
                new X509TrustManager() {
                    @Override
                    public void checkClientTrusted(
                        java.security.cert.X509Certificate[] chain,
                        String authType) {
                    }

                    @Override
                    public void checkServerTrusted(
                        java.security.cert.X509Certificate[] chain,
                        String authType) {
                    }

                    @Override
                    public java.security.cert.X509Certificate[] getAcceptedIssuers() {
                        return new java.security.cert.X509Certificate[0];
                    }
                }
            };
            SSLContext context = SSLContext.getInstance("TLS");
            context.init(null, trustAllManagers, new java.security.SecureRandom());
            return context;
        } catch (java.security.GeneralSecurityException ex) {
            throw new IllegalStateException("failed to create insecure TLS context", ex);
        }
    }

    private CompletableFuture<Void> reconnectAttemptAsync(int attempt, Duration delay) {
        CompletableFuture<Void> result = new CompletableFuture<>();
        TIMEOUTS.schedule(() -> {
            if (state == ZLinkStreamConnectionState.CLOSED) {
                result.completeExceptionally(new IllegalStateException("connector is closed"));
                return;
            }
            connectOnceAsync().whenComplete((ignored, ex) -> {
                if (ex == null) {
                    result.complete(null);
                    return;
                }
                if (reconnectAttemptsExhausted(attempt)) {
                    transitionTo(ZLinkStreamConnectionState.DISCONNECTED);
                    result.completeExceptionally(ex);
                    return;
                }
                reconnectAttemptAsync(attempt + 1, nextReconnectDelay(delay))
                    .whenComplete((retryIgnored, retryEx) -> {
                        if (retryEx == null) {
                            result.complete(null);
                        } else {
                            result.completeExceptionally(retryEx);
                        }
                    });
            });
        }, delay.toMillis(), TimeUnit.MILLISECONDS);
        return result;
    }

    private Duration nextReconnectDelay(Duration current) {
        long nextMillis = Math.round(current.toMillis() * options.reconnectBackoffFactor());
        if (nextMillis <= 0) {
            nextMillis = options.reconnectInitialDelay().toMillis();
        }
        return Duration.ofMillis(Math.min(nextMillis, options.reconnectMaxDelay().toMillis()));
    }

    private void startHeartbeat() {
        stopHeartbeat();
        if (!options.heartbeatEnabled()) {
            return;
        }
        heartbeatTask = TIMEOUTS.scheduleAtFixedRate(
            this::heartbeatTick,
            options.heartbeatInterval().toMillis(),
            options.heartbeatInterval().toMillis(),
            TimeUnit.MILLISECONDS);
    }

    private void stopHeartbeat() {
        ScheduledFuture<?> current = heartbeatTask;
        heartbeatTask = null;
        if (current != null) {
            current.cancel(false);
        }
    }

    private void heartbeatTick() {
        ZLinkStreamTransportConnection current = connection;
        if (current == null || state != ZLinkStreamConnectionState.CONNECTED) {
            return;
        }
        long idleNanos = System.nanoTime() - lastInboundNanos;
        if (idleNanos > options.heartbeatTimeout().toNanos()) {
            handleReceiveFailure(current, new TimeoutException("Heartbeat timed out"));
            return;
        }
        sendControl(HEARTBEAT_PING_NAME);
    }

    private CompletionStage<Void> sendControl(String name) {
        ZLinkStreamWireProtocol.Header header = new ZLinkStreamWireProtocol.Header(
            ZLinkStreamWireProtocol.KIND_CONTROL,
            ZLinkStreamWireProtocol.CODEC_RAW,
            0,
            null,
            name,
            Map.of());
        return sendFrame(header, new byte[0]);
    }

    private void transitionTo(ZLinkStreamConnectionState next) {
        if (state == next) {
            return;
        }
        state = next;
        for (ZLinkStreamConnectionStateHandler handler : List.copyOf(stateHandlers)) {
            handler.handleAsync(next).exceptionally(ex -> null);
        }
    }

    private void ensureConnected() {
        if (!isConnected()) {
            throw new IllegalStateException("connector is not connected");
        }
    }

    private void notifyDisconnected() {
        for (ZLinkStreamDisconnectedHandler handler : List.copyOf(disconnectedHandlers)) {
            handler.handleAsync().exceptionally(ex -> null);
        }
    }

    private void failPending(Throwable ex) {
        pendingRequests.failAll(ex);
    }

    private static ZLinkStreamConnectorOptions validate(
        ZLinkStreamConnectorOptions options) {
        Objects.requireNonNull(options, "options");
        Objects.requireNonNull(options.endpoint(), "endpoint");
        requireSupportedEndpointScheme(options.endpoint().getScheme());
        Objects.requireNonNull(options.dispatchMode(), "dispatchMode");
        Objects.requireNonNull(options.nameResolver(), "nameResolver");
        requirePositive(options.connectTimeout(), "connectTimeout");
        requirePositive(options.requestTimeout(), "requestTimeout");
        requirePositive(options.heartbeatInterval(), "heartbeatInterval");
        requirePositive(options.heartbeatTimeout(), "heartbeatTimeout");
        if (options.heartbeatEnabled()
            && !options.heartbeatTimeout().minus(options.heartbeatInterval()).isPositive()) {
            throw new IllegalArgumentException(
                "heartbeatTimeout must be greater than heartbeatInterval");
        }
        requirePositive(options.reconnectInitialDelay(), "reconnectInitialDelay");
        requirePositive(options.reconnectMaxDelay(), "reconnectMaxDelay");
        if (options.reconnectBackoffFactor() < 1.0) {
            throw new IllegalArgumentException("reconnectBackoffFactor must be at least 1.0");
        }
        if (options.maxReconnectAttempts() < ZLinkStreamConnectorOptions.UNLIMITED_RECONNECT_ATTEMPTS) {
            throw new IllegalArgumentException("maxReconnectAttempts must be unlimited or positive");
        }
        if (options.reconnectEnabled() && options.maxReconnectAttempts() == 0) {
            throw new IllegalArgumentException("maxReconnectAttempts must be unlimited or positive");
        }
        if (options.maxSendPayloadSize() <= 0) {
            throw new IllegalArgumentException("maxSendPayloadSize must be positive");
        }
        Objects.requireNonNull(options.compression(), "compression");
        return options;
    }

    private static void requireSupportedEndpointScheme(String scheme) {
        if (scheme == null || scheme.isBlank()) {
            throw new IllegalArgumentException("endpoint URI scheme is required");
        }
        if (!List.of("tcp", "tls", "ws", "wss").contains(scheme)) {
            throw new IllegalArgumentException("unsupported endpoint URI scheme: " + scheme);
        }
    }

    private static void requirePositive(Duration value, String name) {
        Objects.requireNonNull(value, name);
        if (value.isZero() || value.isNegative()) {
            throw new IllegalArgumentException(name + " must be positive");
        }
    }

    private boolean reconnectAttemptsExhausted(int attempt) {
        int maxAttempts = options.maxReconnectAttempts();
        return maxAttempts != ZLinkStreamConnectorOptions.UNLIMITED_RECONNECT_ATTEMPTS
            && attempt >= maxAttempts;
    }

    static int resolvePort(java.net.URI endpoint) {
        if (endpoint.getPort() > 0) {
            return endpoint.getPort();
        }
        throw new IllegalArgumentException("endpoint port is required");
    }

    private boolean isWebSocketEndpoint() {
        String scheme = options.endpoint().getScheme();
        return "ws".equals(scheme) || "wss".equals(scheme);
    }

    private boolean isTlsEndpoint() {
        return "tls".equals(options.endpoint().getScheme());
    }

    private static byte[] drainPayload(ZLinkStreamEncodedPayload payload) {
        try {
            return payload.payload().toByteArray();
        } finally {
            payload.payload().close();
        }
    }

    private byte[] encodePayload(ZLinkStreamEncodedPayload payload, boolean compress) {
        if (compress && options.compression() != ZLinkStreamCompression.LZ4) {
            throw new IllegalStateException("compression codec is not configured");
        }
        byte[] body = drainPayload(payload);
        if (body.length > options.maxSendPayloadSize()) {
            throw new IllegalArgumentException("payload exceeds max payload size");
        }
        if (!compress) {
            return body;
        }
        return ZLinkStreamLz4Pickler.pickle(body);
    }

    private byte[] decodePayload(ZLinkStreamWireProtocol.Header header, byte[] payload) {
        if ((header.flags() & ZLinkStreamWireProtocol.FLAG_PAYLOAD_COMPRESSED) == 0) {
            return payload;
        }
        if (options.compression() != ZLinkStreamCompression.LZ4) {
            throw new IllegalStateException("compression codec is not configured");
        }
        return ZLinkStreamLz4Pickler.unpickle(payload);
    }

    private static ZLinkStreamEncodedPayload copyPayload(
        ZLinkStreamEncodedPayload payload) {
        Objects.requireNonNull(payload, "payload");
        return new ZLinkStreamEncodedPayload(
            requirePacketName(payload.packetName()),
            Message.from(payload.payload()),
            Map.copyOf(payload.metadata()),
            Objects.requireNonNull(payload.codec(), "codec"));
    }

    private static int toWireCodec(ZLinkStreamCodec codec) {
        return switch (Objects.requireNonNull(codec, "codec")) {
            case RAW -> ZLinkStreamWireProtocol.CODEC_RAW;
            case JSON -> ZLinkStreamWireProtocol.CODEC_JSON;
            case MESSAGE_PACK -> ZLinkStreamWireProtocol.CODEC_MESSAGE_PACK;
            case PROTOBUF -> ZLinkStreamWireProtocol.CODEC_PROTOBUF;
        };
    }

    private static ZLinkStreamCodec fromWireCodec(int codec) {
        return switch (codec) {
            case ZLinkStreamWireProtocol.CODEC_RAW -> ZLinkStreamCodec.RAW;
            case ZLinkStreamWireProtocol.CODEC_JSON -> ZLinkStreamCodec.JSON;
            case ZLinkStreamWireProtocol.CODEC_MESSAGE_PACK -> ZLinkStreamCodec.MESSAGE_PACK;
            case ZLinkStreamWireProtocol.CODEC_PROTOBUF -> ZLinkStreamCodec.PROTOBUF;
            default -> throw new IllegalArgumentException("unknown stream codec");
        };
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

    private static long nextRequestSeq(AtomicLong source) {
        while (true) {
            long value = source.incrementAndGet();
            if (value != 0) {
                return value;
            }
        }
    }

    private long nextRequestSeq() {
        return nextRequestSeq(nextRequestSeq);
    }

    private static void closeQuietly(ZLinkStreamTransportConnection connection) {
        if (connection != null) {
            connection.close();
        }
    }

    private static void closeRawQuietly(AsynchronousSocketChannel channel) {
        if (channel == null) {
            return;
        }
        try {
            channel.close();
        } catch (IOException ignored) {
        }
    }

    private record SendCall(
        DefaultZLinkStreamConnector connector,
        ZLinkStreamEncodedPayload payload,
        boolean compressed) implements ZLinkStreamSendCall {
        @Override
        public ZLinkStreamSendCall packetName(String packetName) {
            return new SendCall(connector, new ZLinkStreamEncodedPayload(
                requirePacketName(packetName),
                payload.payload(),
                payload.metadata(),
                payload.codec()), compressed);
        }

        @Override
        public ZLinkStreamSendCall metadata(String key, String value) {
            Map<String, String> metadata = new HashMap<>(payload.metadata());
            metadata.put(key, value);
            return metadata(metadata);
        }

        @Override
        public ZLinkStreamSendCall metadata(Map<String, String> metadata) {
            return new SendCall(connector, new ZLinkStreamEncodedPayload(
                payload.packetName(),
                payload.payload(),
                Map.copyOf(Objects.requireNonNull(metadata, "metadata")),
                payload.codec()), compressed);
        }

        @Override
        public ZLinkStreamSendCall compress() {
            return new SendCall(connector, payload, true);
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            return connector.submit(payload, compressed);
        }
    }

    private record RequestCall(
        DefaultZLinkStreamConnector connector,
        ZLinkStreamEncodedPayload payload,
        Duration timeout,
        boolean compressed) implements ZLinkStreamRequestCall {
        @Override
        public ZLinkStreamRequestCall packetName(String packetName) {
            return new RequestCall(connector, new ZLinkStreamEncodedPayload(
                requirePacketName(packetName),
                payload.payload(),
                payload.metadata(),
                payload.codec()), timeout, compressed);
        }

        @Override
        public ZLinkStreamRequestCall metadata(String key, String value) {
            Map<String, String> metadata = new HashMap<>(payload.metadata());
            metadata.put(key, value);
            return metadata(metadata);
        }

        @Override
        public ZLinkStreamRequestCall metadata(Map<String, String> metadata) {
            return new RequestCall(connector, new ZLinkStreamEncodedPayload(
                payload.packetName(),
                payload.payload(),
                Map.copyOf(Objects.requireNonNull(metadata, "metadata")),
                payload.codec()), timeout, compressed);
        }

        @Override
        public ZLinkStreamRequestCall compress() {
            return new RequestCall(connector, payload, timeout, true);
        }

        @Override
        public ZLinkStreamRequestCall timeout(Duration timeout) {
            requirePositive(timeout, "timeout");
            return new RequestCall(connector, payload, timeout, compressed);
        }

        @Override
        public CompletionStage<ZLinkStreamEncodedPayload> submitAsync() {
            return connector.submitRequest(payload, timeout, compressed);
        }
    }

    private static final class DaemonThreadFactory implements ThreadFactory {
        @Override
        public Thread newThread(Runnable runnable) {
            Thread thread = new Thread(runnable, "zlink-stream-connector-timeouts");
            thread.setDaemon(true);
            return thread;
        }
    }
}

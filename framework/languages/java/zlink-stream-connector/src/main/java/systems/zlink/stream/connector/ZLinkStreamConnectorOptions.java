package systems.zlink.stream.connector;

import java.net.URI;
import java.time.Duration;

public record ZLinkStreamConnectorOptions(
    URI endpoint,
    ZLinkStreamDispatchMode dispatchMode,
    Duration requestTimeout,
    int maxReconnectAttempts,
    Duration connectTimeout,
    int maxSendPayloadSize,
    boolean heartbeatEnabled,
    Duration heartbeatInterval,
    Duration heartbeatTimeout,
    boolean reconnectEnabled,
    Duration reconnectInitialDelay,
    Duration reconnectMaxDelay,
    double reconnectBackoffFactor,
    boolean skipServerCertificateValidation,
    ZLinkStreamCompression compression,
    ZLinkStreamPacketNameResolver nameResolver) {
    public static final int UNLIMITED_RECONNECT_ATTEMPTS = -1;

    public ZLinkStreamConnectorOptions {
        if (compression == null) {
            compression = ZLinkStreamCompression.NONE;
        }
        if (nameResolver == null) {
            nameResolver = ZLinkStreamPacketNameResolver.defaultResolver();
        }
    }

    public ZLinkStreamConnectorOptions(
        URI endpoint,
        ZLinkStreamDispatchMode dispatchMode,
        Duration requestTimeout,
        int maxReconnectAttempts,
        Duration connectTimeout,
        int maxSendPayloadSize,
        boolean heartbeatEnabled,
        Duration heartbeatInterval,
        Duration heartbeatTimeout,
        boolean reconnectEnabled,
        Duration reconnectInitialDelay,
        Duration reconnectMaxDelay,
        double reconnectBackoffFactor,
        boolean skipServerCertificateValidation) {
        this(
            endpoint,
            dispatchMode,
            requestTimeout,
            maxReconnectAttempts,
            connectTimeout,
            maxSendPayloadSize,
            heartbeatEnabled,
            heartbeatInterval,
            heartbeatTimeout,
            reconnectEnabled,
            reconnectInitialDelay,
            reconnectMaxDelay,
            reconnectBackoffFactor,
            skipServerCertificateValidation,
            ZLinkStreamCompression.NONE);
    }

    public ZLinkStreamConnectorOptions(
        URI endpoint,
        ZLinkStreamDispatchMode dispatchMode,
        Duration requestTimeout,
        int maxReconnectAttempts,
        Duration connectTimeout,
        int maxSendPayloadSize,
        boolean heartbeatEnabled,
        Duration heartbeatInterval,
        Duration heartbeatTimeout,
        boolean reconnectEnabled,
        Duration reconnectInitialDelay,
        Duration reconnectMaxDelay,
        double reconnectBackoffFactor,
        boolean skipServerCertificateValidation,
        ZLinkStreamCompression compression) {
        this(
            endpoint,
            dispatchMode,
            requestTimeout,
            maxReconnectAttempts,
            connectTimeout,
            maxSendPayloadSize,
            heartbeatEnabled,
            heartbeatInterval,
            heartbeatTimeout,
            reconnectEnabled,
            reconnectInitialDelay,
            reconnectMaxDelay,
            reconnectBackoffFactor,
            skipServerCertificateValidation,
            compression,
            ZLinkStreamPacketNameResolver.defaultResolver());
    }

    public ZLinkStreamConnectorOptions(
        URI endpoint,
        ZLinkStreamDispatchMode dispatchMode,
        Duration requestTimeout,
        int maxReconnectAttempts,
        Duration connectTimeout,
        int maxSendPayloadSize,
        boolean heartbeatEnabled,
        Duration heartbeatInterval,
        Duration heartbeatTimeout,
        boolean reconnectEnabled,
        Duration reconnectInitialDelay,
        Duration reconnectMaxDelay,
        double reconnectBackoffFactor,
        boolean skipServerCertificateValidation,
        ZLinkStreamPacketNameResolver nameResolver) {
        this(
            endpoint,
            dispatchMode,
            requestTimeout,
            maxReconnectAttempts,
            connectTimeout,
            maxSendPayloadSize,
            heartbeatEnabled,
            heartbeatInterval,
            heartbeatTimeout,
            reconnectEnabled,
            reconnectInitialDelay,
            reconnectMaxDelay,
            reconnectBackoffFactor,
            skipServerCertificateValidation,
            ZLinkStreamCompression.NONE,
            nameResolver);
    }

    public ZLinkStreamConnectorOptions(
        URI endpoint,
        ZLinkStreamDispatchMode dispatchMode,
        Duration requestTimeout,
        int maxReconnectAttempts,
        Duration connectTimeout,
        int maxSendPayloadSize,
        boolean heartbeatEnabled,
        Duration heartbeatInterval,
        Duration heartbeatTimeout,
        boolean reconnectEnabled,
        Duration reconnectInitialDelay,
        Duration reconnectMaxDelay,
        double reconnectBackoffFactor) {
        this(
            endpoint,
            dispatchMode,
            requestTimeout,
            maxReconnectAttempts,
            connectTimeout,
            maxSendPayloadSize,
            heartbeatEnabled,
            heartbeatInterval,
            heartbeatTimeout,
            reconnectEnabled,
            reconnectInitialDelay,
            reconnectMaxDelay,
            reconnectBackoffFactor,
            false);
    }

    public ZLinkStreamConnectorOptions(
        URI endpoint,
        ZLinkStreamDispatchMode dispatchMode,
        Duration requestTimeout,
        int maxReconnectAttempts,
        Duration connectTimeout,
        int maxSendPayloadSize) {
        this(
            endpoint,
            dispatchMode,
            requestTimeout,
            maxReconnectAttempts,
            connectTimeout,
            maxSendPayloadSize,
            true,
            Duration.ofSeconds(1),
            Duration.ofSeconds(5),
            true,
            Duration.ofMillis(250),
            Duration.ofSeconds(5),
            2.0,
            false);
    }

    public ZLinkStreamConnectorOptions(
        URI endpoint,
        ZLinkStreamDispatchMode dispatchMode,
        Duration requestTimeout,
        int maxReconnectAttempts) {
        this(
            endpoint,
            dispatchMode,
            requestTimeout,
            maxReconnectAttempts,
            Duration.ofSeconds(5),
            64 * 1024);
    }
}

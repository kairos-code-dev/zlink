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
    ZLinkStreamPacketNameResolver nameResolver) {
    public ZLinkStreamConnectorOptions {
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

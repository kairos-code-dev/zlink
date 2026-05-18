using System.Net.Security;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Authentication;

namespace Systems.Zlink.Stream.Connector.Runtime;

internal static class ZlinkStreamTransportFactory
{
    public static void ValidateOptions(ZlinkStreamConnectorOptions options)
    {
        if (options.Endpoint is null)
        {
            throw new ArgumentException("Endpoint is required.", nameof(options));
        }

        _ = ResolveTransport(options);

        if (options.ConnectTimeout <= TimeSpan.Zero)
        {
            throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.ValidationFailed,
                "ConnectTimeout must be positive.");
        }

        if (options.RequestTimeout <= TimeSpan.Zero)
        {
            throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.ValidationFailed,
                "RequestTimeout must be positive.");
        }

        if (options.Heartbeat is { } heartbeat)
        {
            if (heartbeat.Interval <= TimeSpan.Zero)
            {
                throw ZlinkStreamConnector.Error(
                    ZlinkStreamErrorCode.ValidationFailed,
                    "Heartbeat interval must be positive.");
            }

            if (heartbeat.Timeout <= TimeSpan.Zero)
            {
                throw ZlinkStreamConnector.Error(
                    ZlinkStreamErrorCode.ValidationFailed,
                    "Heartbeat timeout must be positive.");
            }

            if (heartbeat.Timeout <= heartbeat.Interval)
            {
                throw ZlinkStreamConnector.Error(
                    ZlinkStreamErrorCode.ValidationFailed,
                    "Heartbeat timeout must be greater than the heartbeat interval.");
            }
        }

        if (options.Reconnect is { } reconnect)
        {
            if (reconnect.InitialDelay <= TimeSpan.Zero)
            {
                throw ZlinkStreamConnector.Error(
                    ZlinkStreamErrorCode.ValidationFailed,
                    "Reconnect InitialDelay must be positive.");
            }

            if (reconnect.MaxDelay <= TimeSpan.Zero)
            {
                throw ZlinkStreamConnector.Error(
                    ZlinkStreamErrorCode.ValidationFailed,
                    "Reconnect MaxDelay must be positive.");
            }

            if (reconnect.BackoffFactor < 1.0)
            {
                throw ZlinkStreamConnector.Error(
                    ZlinkStreamErrorCode.ValidationFailed,
                    "Reconnect BackoffFactor must be at least 1.0.");
            }

            if (reconnect.MaxAttempts <= 0)
            {
                throw ZlinkStreamConnector.Error(
                    ZlinkStreamErrorCode.ValidationFailed,
                    "Reconnect MaxAttempts must be null or positive.");
            }
        }

        if (options.MaxSendFrameSize <= 0)
        {
            throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.ValidationFailed,
                "MaxSendFrameSize must be positive.");
        }

    }

    public static async ValueTask<IZlinkStreamConnection> ConnectAsync(
        ZlinkStreamConnectorOptions options,
        CancellationToken cancellationToken)
    {
        var transport = ResolveTransport(options);
        return transport is ZlinkStreamTransport.WebSocket or ZlinkStreamTransport.WebSocketSecure
            ? await ConnectWebSocketAsync(options, cancellationToken).ConfigureAwait(false)
            : await ConnectStreamAsync(options, transport, cancellationToken).ConfigureAwait(false);
    }

    private static async ValueTask<IZlinkStreamConnection> ConnectWebSocketAsync(
        ZlinkStreamConnectorOptions options,
        CancellationToken cancellationToken)
    {
        var webSocket = new ClientWebSocket();
        if (options.SkipServerCertificateValidation)
        {
            webSocket.Options.RemoteCertificateValidationCallback = (_, _, _, _) => true;
        }

        await webSocket.ConnectAsync(options.Endpoint, cancellationToken).ConfigureAwait(false);
        return new WebSocketConnection(webSocket);
    }

    private static async ValueTask<IZlinkStreamConnection> ConnectStreamAsync(
        ZlinkStreamConnectorOptions options,
        ZlinkStreamTransport transport,
        CancellationToken cancellationToken)
    {
        var tcp = new TcpClient();
        tcp.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.KeepAlive, true);
        await tcp.ConnectAsync(options.Endpoint.Host, options.Endpoint.Port, cancellationToken).ConfigureAwait(false);
        System.IO.Stream stream = tcp.GetStream();
        if (transport == ZlinkStreamTransport.Tls)
        {
            var ssl = new SslStream(
                stream,
                false,
                options.SkipServerCertificateValidation
                    ? (_, _, _, _) => true
                    : null);
            await ssl.AuthenticateAsClientAsync(options.Endpoint.Host).WaitAsync(cancellationToken).ConfigureAwait(false);
            stream = ssl;
        }

        return new StreamConnection(tcp, stream);
    }

    private static ZlinkStreamTransport ResolveTransport(ZlinkStreamConnectorOptions options)
    {
        var inferred = options.Endpoint.Scheme.ToLowerInvariant() switch
        {
            "tcp" => ZlinkStreamTransport.Tcp,
            "tls" => ZlinkStreamTransport.Tls,
            "ws" => ZlinkStreamTransport.WebSocket,
            "wss" => ZlinkStreamTransport.WebSocketSecure,
            _ => throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.ConfigurationError,
                "Endpoint scheme is not supported.")
        };

        if (options.Transport is { } configured && configured != inferred)
        {
            throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.ConfigurationError,
                "Configured transport conflicts with endpoint scheme.");
        }

        return inferred;
    }
}

using System.Net.Security;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Authentication;

namespace Systems.Zlink.Stream.Connector.Connector;

internal static class ZlinkStreamTransportFactory
{
    public static void ValidateOptions(ZlinkStreamConnectorOptions options)
    {
        if (options.Endpoint is null)
        {
            throw new ArgumentException("Endpoint is required.", nameof(options));
        }

        if (options.MaxSendFrameSize <= 0)
        {
            throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.ValidationFailed,
                "MaxSendFrameSize must be positive.");
        }

        if (options.MaxSendMetadataSize < 0)
        {
            throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.ValidationFailed,
                "MaxSendMetadataSize must not be negative.");
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
        webSocket.Options.KeepAliveInterval = options.HeartbeatInterval;
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
        global::System.IO.Stream stream = tcp.GetStream();
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

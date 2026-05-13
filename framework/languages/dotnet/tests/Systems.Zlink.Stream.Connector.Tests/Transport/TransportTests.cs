using System.Buffers.Binary;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Calls;
using Systems.Zlink.Stream.Connector.Protocol;
using Systems.Zlink.Stream.Connector.Protocol.Compression;
using Systems.Zlink.Stream.Connector.Runtime;
using Systems.Zlink.Stream.Connector.Protocol.Framing;
using Xunit;


public sealed partial class StreamConnectorTests
{
    [Fact]
    public async Task TcpSendUsesHeaderBodyFrame()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = new ZlinkStreamHeaderCodec();
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            var packet = await ReadPacketAsync(stream);
            var header = headerCodec.Decode(packet.Header);
            Assert.Equal("h", header.Name);
            Assert.Equal(ZlinkStreamCodec.Raw, header.Codec);
            Assert.Equal("b", Encoding.UTF8.GetString(packet.Body));
        });

        await using var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}")
        });

        await connector.Send(new ZlinkStreamEncodedBody(ZlinkStreamCodec.Raw, "b"u8.ToArray()))
            .WithPacketName("h")
            .Submit();

        await server;
    }

    [Fact]
    public async Task TcpReceiveDispatchesMultipleHeaderPacketsInOrder()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = new ZlinkStreamHeaderCodec();
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            await WritePacketAsync(
                stream,
                headerCodec.Encode(new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "h1",
                    ZlinkStreamMetadata.Empty)).ToArray(),
                "b1"u8.ToArray());
            await WritePacketAsync(
                stream,
                headerCodec.Encode(new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "h2",
                    ZlinkStreamMetadata.Empty)).ToArray(),
                "b2"u8.ToArray());
        });

        await using var connector = new ZlinkStreamConnector(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}")
        });
        var received = new List<string>();
        var first = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var second = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.On("h1", (message, _) =>
        {
            received.Add($"{message.Name}:{Encoding.UTF8.GetString(message.Body.Body.Span)}");
            first.SetResult();
            return ValueTask.CompletedTask;
        });
        connector.On("h2", (message, _) =>
        {
            received.Add($"{message.Name}:{Encoding.UTF8.GetString(message.Body.Body.Span)}");
            second.SetResult();
            return ValueTask.CompletedTask;
        });

        await connector.ConnectAsync();
        await first.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await second.Task.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(["h1:b1", "h2:b2"], received);
        await server;
    }

    [Fact]
    public async Task WebSocketSendUsesBinaryFrames()
    {
        using var listener = new HttpListener();
        var port = GetFreeTcpPort();
        listener.Prefixes.Add($"http://127.0.0.1:{port}/ws/");
        listener.Start();
        var headerCodec = new ZlinkStreamHeaderCodec();
        var server = Task.Run(async () =>
        {
            var context = await listener.GetContextAsync();
            var webSocketContext = await context.AcceptWebSocketAsync(null);
            using var webSocket = webSocketContext.WebSocket;
            var received = await ReceiveWebSocketMessageAsync(webSocket);
            var packet = DecodePacket(received);
            var header = headerCodec.Decode(packet.Header);
            Assert.Equal("wh", header.Name);
            Assert.Equal(ZlinkStreamCodec.Raw, header.Codec);
            Assert.Equal("wb", Encoding.UTF8.GetString(packet.Body));
        });

        await using var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"ws://127.0.0.1:{port}/ws/")
        });

        await connector.Send(new ZlinkStreamEncodedBody(ZlinkStreamCodec.Raw, "wb"u8.ToArray()))
            .WithPacketName("wh")
            .Submit();

        await server;
    }

    [Fact]
    public async Task TlsSendWorksWithSkippedCertificateValidation()
    {
        using var certificate = CreateSelfSignedCertificate();
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = new ZlinkStreamHeaderCodec();
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var ssl = new SslStream(tcp.GetStream(), false);
            await ssl.AuthenticateAsServerAsync(certificate);
            var packet = await ReadPacketAsync(ssl);
            var header = headerCodec.Decode(packet.Header);
            Assert.Equal("th", header.Name);
            Assert.Equal(ZlinkStreamCodec.Raw, header.Codec);
            Assert.Equal("tb", Encoding.UTF8.GetString(packet.Body));
        });

        await using var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tls://127.0.0.1:{endpoint.Port}"),
            SkipServerCertificateValidation = true
        });

        await connector.Send(new ZlinkStreamEncodedBody(ZlinkStreamCodec.Raw, "tb"u8.ToArray()))
            .WithPacketName("th")
            .Submit();

        await server;
    }

}

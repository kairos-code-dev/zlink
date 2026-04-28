using System.Buffers.Binary;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using Systems.Zlink.Stream.Connector.Abstractions;
using Systems.Zlink.Stream.Connector.Builders;
using Systems.Zlink.Stream.Connector.Codecs;
using Systems.Zlink.Stream.Connector.Compression;
using Systems.Zlink.Stream.Connector.Connector;
using Systems.Zlink.Stream.Connector.Framing;
using Systems.Zlink.Stream.Connector.Headers;
using Systems.Zlink.Stream.Connector.Metadata;
using Systems.Zlink.Stream.Connector.Options;
using Xunit;


public sealed partial class StreamConnectorTests
{
    [Fact]
    public async Task TcpRawSendAndReceiveUsesHeaderBodyFrame()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            var packet = await ReadPacketAsync(stream);
            Assert.Equal("h", Encoding.UTF8.GetString(packet.Header));
            Assert.Equal("b", Encoding.UTF8.GetString(packet.Body));
            await WritePacketAsync(stream, "eh"u8.ToArray(), "eb"u8.ToArray());
        });

        await using var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}")
        });

        connector.SendRaw("h"u8.ToArray(), "b"u8.ToArray());
        var reply = await connector.ReceiveRawAsync();

        Assert.Equal("eh", Encoding.UTF8.GetString(reply.Header.Span));
        Assert.Equal("eb", Encoding.UTF8.GetString(reply.Body.Span));
        await server;
    }

    [Fact]
    public async Task TcpReceiveHandlesMultiplePacketsInOrder()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            await WritePacketAsync(stream, "h1"u8.ToArray(), "b1"u8.ToArray());
            await WritePacketAsync(stream, "h2"u8.ToArray(), "b2"u8.ToArray());
        });

        await using var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}")
        });

        var first = await connector.ReceiveRawAsync();
        var second = await connector.ReceiveRawAsync();

        Assert.Equal("h1", Encoding.UTF8.GetString(first.Header.Span));
        Assert.Equal("b1", Encoding.UTF8.GetString(first.Body.Span));
        Assert.Equal("h2", Encoding.UTF8.GetString(second.Header.Span));
        Assert.Equal("b2", Encoding.UTF8.GetString(second.Body.Span));
        await server;
    }

    [Fact]
    public async Task WebSocketRawSendAndReceiveUsesBinaryFrames()
    {
        using var listener = new HttpListener();
        var port = GetFreeTcpPort();
        listener.Prefixes.Add($"http://127.0.0.1:{port}/ws/");
        listener.Start();
        var server = Task.Run(async () =>
        {
            var context = await listener.GetContextAsync();
            var webSocketContext = await context.AcceptWebSocketAsync(null);
            using var webSocket = webSocketContext.WebSocket;
            var received = await ReceiveWebSocketMessageAsync(webSocket);
            var packet = DecodePacket(received);
            Assert.Equal("wh", Encoding.UTF8.GetString(packet.Header));
            Assert.Equal("wb", Encoding.UTF8.GetString(packet.Body));
            await webSocket.SendAsync(
                EncodePacket("rh"u8.ToArray(), "rb"u8.ToArray()),
                WebSocketMessageType.Binary,
                true,
                CancellationToken.None);
        });

        await using var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"ws://127.0.0.1:{port}/ws/")
        });

        connector.SendRaw("wh"u8.ToArray(), "wb"u8.ToArray());
        var reply = await connector.ReceiveRawAsync();

        Assert.Equal("rh", Encoding.UTF8.GetString(reply.Header.Span));
        Assert.Equal("rb", Encoding.UTF8.GetString(reply.Body.Span));
        await server;
    }

    [Fact]
    public async Task TlsRawSendWorksWithSkippedCertificateValidation()
    {
        using var certificate = CreateSelfSignedCertificate();
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var ssl = new SslStream(tcp.GetStream(), false);
            await ssl.AuthenticateAsServerAsync(certificate);
            var packet = await ReadPacketAsync(ssl);
            Assert.Equal("th", Encoding.UTF8.GetString(packet.Header));
            Assert.Equal("tb", Encoding.UTF8.GetString(packet.Body));
            await WritePacketAsync(ssl, "trh"u8.ToArray(), "trb"u8.ToArray());
        });

        await using var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tls://127.0.0.1:{endpoint.Port}"),
            SkipServerCertificateValidation = true
        });

        connector.SendRaw("th"u8.ToArray(), "tb"u8.ToArray());
        var reply = await connector.ReceiveRawAsync();

        Assert.Equal("trh", Encoding.UTF8.GetString(reply.Header.Span));
        Assert.Equal("trb", Encoding.UTF8.GetString(reply.Body.Span));
        await server;
    }

}

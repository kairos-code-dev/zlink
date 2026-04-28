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
    public async Task TcpTypedRequestCorrelatesResponse()
    {
        var headerCodec = new ZlinkStreamHeaderCodec();
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            var packet = await ReadPacketAsync(stream);
            var requestHeader = headerCodec.Decode(packet.Header);

            Assert.Equal(ZlinkStreamMessageKind.Request, requestHeader.Kind);
            Assert.NotNull(requestHeader.RequestId);
            Assert.Equal("ping", requestHeader.Name);

            var responseHeader = new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Response,
                ZlinkStreamCodec.Json,
                ZlinkStreamHeaderFlags.HasRid,
                requestHeader.RequestId,
                requestHeader.Name,
                ZlinkStreamMetadata.Empty);
            var responseBody = JsonSerializer.SerializeToUtf8Bytes(new Pong("pong"));
            await WritePacketAsync(stream, headerCodec.Encode(responseHeader).ToArray(), responseBody);
        });

        await using var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}")
        });

        var reply = await connector
            .Request("ping", new Ping("hello"))
            .WithTimeout(TimeSpan.FromSeconds(1))
            .ExecAsync<Pong>();

        Assert.Equal("pong", reply.Text);
        await server;
    }

    [Fact]
    public async Task CallbackRequestReturnsTypedResult()
    {
        var headerCodec = new ZlinkStreamHeaderCodec();
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            var packet = await ReadPacketAsync(stream);
            var requestHeader = headerCodec.Decode(packet.Header);
            var responseHeader = new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Response,
                ZlinkStreamCodec.Json,
                ZlinkStreamHeaderFlags.HasRid,
                requestHeader.RequestId,
                requestHeader.Name,
                ZlinkStreamMetadata.Empty);
            await WritePacketAsync(
                stream,
                headerCodec.Encode(responseHeader).ToArray(),
                JsonSerializer.SerializeToUtf8Bytes(new Pong("callback")));
        });

        await using var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}")
        });

        var completed = new TaskCompletionSource<ZlinkStreamResult<Pong>>(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.Request("ping", new Ping("hello")).Exec<Pong>(completed.SetResult);

        var result = await completed.Task.WaitAsync(TimeSpan.FromSeconds(1));
        Assert.True(result.IsSuccess);
        Assert.Equal("callback", result.Value?.Text);
        await server;
    }

    [Fact]
    public async Task PacketNameAttributeIsUsedByDefault()
    {
        var headerCodec = new ZlinkStreamHeaderCodec();
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            var packet = await ReadPacketAsync(stream);
            var header = headerCodec.Decode(packet.Header);
            Assert.Equal("custom.packet", header.Name);
        });

        await using var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}")
        });

        connector.Send(new NamedPacket("name")).Exec();
        await server;
    }

    [Fact]
    public async Task MetadataSendLimitIsEnforced()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await Task.Delay(TimeSpan.FromMilliseconds(100));
        });

        await using var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            MaxSendMetadataSize = 4
        });

        var exception = Assert.Throws<ZlinkStreamException>(() =>
            connector.Send("ping", new Ping("hello")).Metadata("traceId", "abcdef").Exec());

        Assert.Equal(ZlinkStreamErrorCode.ValidationFailed, exception.Error.Code);
        await server;
    }

    [Fact]
    public async Task ClientToServerCompressionIsExplicit()
    {
        var headerCodec = new ZlinkStreamHeaderCodec();
        var compressionCodec = new ZlinkStreamLz4CompressionCodec();
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            var plainPacket = await ReadPacketAsync(stream);
            var plainHeader = headerCodec.Decode(plainPacket.Header);
            Assert.False(plainHeader.Flags.HasFlag(ZlinkStreamHeaderFlags.BodyCompressed));

            var compressedPacket = await ReadPacketAsync(stream);
            var compressedHeader = headerCodec.Decode(compressedPacket.Header);
            Assert.True(compressedHeader.Flags.HasFlag(ZlinkStreamHeaderFlags.BodyCompressed));
            var body = compressionCodec.Decompress(compressedPacket.Body);
            var decoded = JsonSerializer.Deserialize<Ping>(body.Span, new JsonSerializerOptions(JsonSerializerDefaults.Web));
            Assert.Equal("compressed", decoded?.Text);
        });

        await using var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Compression = ZlinkStreamCompression.Lz4
        });

        connector.Send("plain", new Ping("plain")).Exec();
        connector.Send("compressed", new Ping("compressed")).Compress().Exec();
        await server;
    }


}

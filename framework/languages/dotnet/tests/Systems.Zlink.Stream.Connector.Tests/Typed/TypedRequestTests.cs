using System.Buffers.Binary;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using Systems.Zlink.Stream.Connector;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Codecs;
using Systems.Zlink.Stream.Connector.Contracts.Calls;
using Systems.Zlink.Stream.Connector.Runtime;
using Systems.Zlink.Stream.Connector.Runtime.Protocol.Framing;
using Xunit;


public sealed partial class StreamConnectorTests
{
    [Fact]
    public async Task TcpTypedRequestCorrelatesResponse()
    {
        var headerCodec = ZlinkStreamDefaultCodecFactory.Header();
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
            Assert.NotNull(requestHeader.RequestSeq);
            Assert.Equal("ping", requestHeader.Name);

            var responseHeader = new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Response,
                ZlinkStreamCodec.Json,
                ZlinkStreamHeaderFlags.HasRequestSeq,
                requestHeader.RequestSeq,
                "pong.res",
                ZlinkStreamMetadata.Empty);
            var responsePayload = JsonSerializer.SerializeToUtf8Bytes(new Pong("pong"));
            await WritePacketAsync(stream, headerCodec.Encode(responseHeader).ToArray(), responsePayload);
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate
        });
        await connector.ConnectAsync();

        var reply = await connector
            .Request(new Ping("hello"))
            .PacketName("ping")
            .Timeout(TimeSpan.FromSeconds(1))
            .SubmitAsync<Pong>();

        Assert.Equal("pong", reply.Text);
        await server;
    }

    [Fact]
    public async Task CallbackRequestReturnsTypedResult()
    {
        var headerCodec = ZlinkStreamDefaultCodecFactory.Header();
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
                ZlinkStreamHeaderFlags.HasRequestSeq,
                requestHeader.RequestSeq,
                "callback.res",
                ZlinkStreamMetadata.Empty);
            await WritePacketAsync(
                stream,
                headerCodec.Encode(responseHeader).ToArray(),
                JsonSerializer.SerializeToUtf8Bytes(new Pong("callback")));
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate
        });
        await connector.ConnectAsync();

        var completed = new TaskCompletionSource<ZlinkStreamResult<Pong>>(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.Request(new Ping("hello"))
            .PacketName("ping")
            .Submit<Pong>(result => completed.SetResult(result));

        var result = await completed.Task.WaitAsync(TimeSpan.FromSeconds(1));
        Assert.True(result.IsSuccess);
        Assert.Equal("callback", result.Value?.Text);
        await server;
    }

    [Fact]
    public async Task PacketNameAttributeIsUsedByDefault()
    {
        var headerCodec = ZlinkStreamDefaultCodecFactory.Header();
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

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}")
        });
        await connector.ConnectAsync();

        await connector.Send(new NamedPacket("name")).Submit();
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

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}")
        });
        await connector.ConnectAsync();

        var exception = await Assert.ThrowsAsync<ZlinkStreamException>(async () =>
            await connector.Send(new Ping("hello"))
                .PacketName("ping")
                .Metadata("traceId", new string('x', 1014))
                .Submit());

        Assert.Equal(ZlinkStreamErrorCode.ValidationFailed, exception.Error.Code);
        await server;
    }

    [Fact]
    public async Task ClientToServerCompressionIsExplicit()
    {
        var headerCodec = ZlinkStreamDefaultCodecFactory.Header();
        var compressionCodec = ZlinkStreamDefaultCodecFactory.Lz4Compression();
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            var plainPacket = await ReadPacketAsync(stream);
            var plainHeader = headerCodec.Decode(plainPacket.Header);
            Assert.False(plainHeader.Flags.HasFlag(ZlinkStreamHeaderFlags.PayloadCompressed));

            var compressedPacket = await ReadPacketAsync(stream);
            var compressedHeader = headerCodec.Decode(compressedPacket.Header);
            Assert.True(compressedHeader.Flags.HasFlag(ZlinkStreamHeaderFlags.PayloadCompressed));
            var payload = compressionCodec.Decompress(compressedPacket.Payload);
            var decoded = JsonSerializer.Deserialize<Ping>(payload.Span, new JsonSerializerOptions(JsonSerializerDefaults.Web));
            Assert.Equal("compressed", decoded?.Text);
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Compression = ZlinkStreamCompression.Lz4
        });
        await connector.ConnectAsync();

        await connector.Send(new Ping("plain"))
            .PacketName("plain")
            .Submit();
        await connector.Send(new Ping("compressed"))
            .PacketName("compressed")
            .Compress()
            .Submit();
        await server;
    }


}

using System.Buffers.Binary;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using MessagePack;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Codecs;
using Systems.Zlink.Stream.Connector.Calls;
using Systems.Zlink.Stream.Connector.Protocol;
using Systems.Zlink.Stream.Connector.Protocol.Compression;
using Systems.Zlink.Stream.Connector.Runtime;
using Systems.Zlink.Stream.Connector.Protocol.Framing;
using Xunit;
using StreamJson = Systems.Zlink.Stream.Connector.Json.ZlinkStreamJsonExtensions;


public sealed partial class StreamConnectorTests
{
    [Fact]
    public void JsonExtensionBuildsEncodedBody()
    {
        var body = StreamJson.ToJson(new Ping("hello"));

        Assert.Equal(ZlinkStreamCodec.Json, body.Codec);
        Assert.Equal(typeof(Ping), body.MessageType);
        Assert.Equal("hello", StreamJson.FromJson<Ping>(body).Text);
    }

    [Fact]
    public async Task AutoCodecUsesMessagePackObjectAttribute()
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
            Assert.Equal(ZlinkStreamCodec.MessagePack, header.Codec);
        });

        await using var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}")
        });

        await connector.Send(new PackedPing { Text = "hello" })
            .WithPacketName("packed")
            .Submit();
        await server;
    }

    [Fact]
    public async Task TypedCallbackDecompressesServerPacket()
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
            var header = new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Send,
                ZlinkStreamCodec.Json,
                ZlinkStreamHeaderFlags.BodyCompressed,
                null,
                "pong",
                ZlinkStreamMetadata.Empty);
            var body = compressionCodec.Compress(JsonSerializer.SerializeToUtf8Bytes(new Pong("compressed")));
            await WritePacketAsync(stream, headerCodec.Encode(header).ToArray(), body.ToArray());
        });

        await using var connector = new ZlinkStreamConnector(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Compression = ZlinkStreamCompression.Lz4
        });
        var received = new TaskCompletionSource<Pong>(TaskCreationOptions.RunContinuationsAsynchronously);
        using var subscription = connector.On<Pong>("pong", (message, _) =>
        {
            received.SetResult(message.Body);
            return ValueTask.CompletedTask;
        });

        await connector.ConnectAsync();

        var reply = await received.Task.WaitAsync(TimeSpan.FromSeconds(1));
        Assert.Equal("compressed", reply.Text);
        await server;
    }


}

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
using Systems.Zlink.Stream.Connector;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Codecs;
using Systems.Zlink.Stream.Connector.Contracts.Calls;
using Systems.Zlink.Stream.Connector.Runtime;
using Systems.Zlink.Stream.Connector.Runtime.Protocol.Framing;
using Xunit;
using StreamJson = Systems.Zlink.Stream.Connector.Json.ZlinkStreamJsonExtensions;


public sealed partial class StreamConnectorTests
{
    [Fact]
    public void JsonExtensionBuildsEncodedPayload()
    {
        var payload = StreamJson.ToJson(new Ping("hello"));

        Assert.Equal(ZlinkStreamCodec.Json, payload.Codec);
        Assert.Equal(typeof(Ping), payload.MessageType);
        Assert.Equal("hello", StreamJson.FromJson<Ping>(payload).Text);
    }

    [Fact]
    public async Task AutoCodecUsesMessagePackObjectAttribute()
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
            Assert.Equal(ZlinkStreamCodec.MessagePack, header.Codec);
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Heartbeat = DisabledHeartbeat()
        });
        await connector.ConnectAsync();

        await connector.Send(new PackedPing { Text = "hello" })
            .PacketName("packed")
            .SubmitAsync();
        await server;
    }

    [Fact]
    public async Task TypedCallbackDecompressesServerPacket()
    {
        var headerCodec = ZlinkStreamDefaultCodecFactory.Header();
        var compressionCodec = ZlinkStreamDefaultCodecFactory.Lz4Compression();
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var received = new TaskCompletionSource<Pong>(TaskCreationOptions.RunContinuationsAsynchronously);
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            var header = new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Send,
                ZlinkStreamCodec.Json,
                ZlinkStreamHeaderFlags.PayloadCompressed,
                null,
                "pong",
                ZlinkStreamMetadata.Empty);
            var payload = compressionCodec.Compress(JsonSerializer.SerializeToUtf8Bytes(new Pong("compressed")));
            await WritePacketAsync(stream, headerCodec.Encode(header).ToArray(), payload.ToArray());
            await received.Task.WaitAsync(TimeSpan.FromSeconds(5));
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Heartbeat = DisabledHeartbeat(),
            Compression = ZlinkStreamCompression.Lz4,
            DispatchMode = ZlinkStreamDispatchMode.Immediate
        });
        using var subscription = connector.On<Pong>("pong", (message, _) =>
        {
            received.SetResult(message.Payload);
            return ValueTask.CompletedTask;
        });

        await connector.ConnectAsync();

        var reply = await received.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal("compressed", reply.Text);
        await server;
    }


}

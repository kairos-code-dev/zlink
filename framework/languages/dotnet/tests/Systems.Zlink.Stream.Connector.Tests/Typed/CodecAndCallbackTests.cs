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
    public void CodecRegistryUsesTypeSpecificCodecBeforeDefaultCodec()
    {
        var registry = new ZlinkStreamCodecRegistry(
            ZlinkStreamCodec.Json,
            new IZlinkStreamBodyCodec[]
            {
                new ZlinkStreamJsonBodyCodec(),
                new SpecificPingCodec()
            });

        var codec = registry.ResolveForSend(typeof(Ping));

        Assert.Equal(ZlinkStreamCodec.Raw, codec.Codec);
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

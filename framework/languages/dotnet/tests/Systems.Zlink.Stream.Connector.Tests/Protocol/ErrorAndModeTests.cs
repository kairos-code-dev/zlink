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
    public async Task RequestTimeoutRemovesPendingRequest()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            _ = await ReadPacketAsync(stream);
            await Task.Delay(TimeSpan.FromMilliseconds(200));
        });

        await using var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            RequestTimeout = TimeSpan.FromMilliseconds(50)
        });

        var exception = await Assert.ThrowsAsync<ZlinkStreamException>(async () =>
            await connector.Request("ping", new Ping("hello")).ExecAsync<Pong>());

        Assert.Equal(ZlinkStreamErrorCode.RequestTimeout, exception.Error.Code);
        await server;
    }

    [Fact]
    public async Task DisconnectedSendFailsBeforeTransportWrite()
    {
        await using var connector = new ZlinkStreamConnector(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri("tcp://127.0.0.1:1")
        });

        var exception = Assert.Throws<ZlinkStreamException>(() =>
            connector.SendRaw("h"u8.ToArray(), "b"u8.ToArray()));

        Assert.Equal(ZlinkStreamErrorCode.Disconnected, exception.Error.Code);
    }

    [Fact]
    public async Task SendFrameLimitIsEnforcedBeforeTransportWrite()
    {
        await using var connector = new ZlinkStreamConnector(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri("tcp://127.0.0.1:1"),
            MaxSendFrameSize = 6
        });

        var exception = Assert.Throws<ZlinkStreamException>(() =>
            connector.SendRaw("h"u8.ToArray(), "b"u8.ToArray()));

        Assert.Equal(ZlinkStreamErrorCode.FrameTooLarge, exception.Error.Code);
    }

    [Fact]
    public async Task ReceiveRawCannotBeMixedWithCallbackReceive()
    {
        await using var connector = new ZlinkStreamConnector(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri("tcp://127.0.0.1:1")
        });
        connector.PacketReceived += (_, _) => ValueTask.CompletedTask;

        var exception = await Assert.ThrowsAsync<ZlinkStreamException>(async () =>
            await connector.ReceiveRawAsync());

        Assert.Equal(ZlinkStreamErrorCode.ConfigurationError, exception.Error.Code);
    }

}

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
using Systems.Zlink.Stream.Connector.Protocol.Framing;
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

        await using var connector = await ZlinkStreamConnectorFactory.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            RequestTimeout = TimeSpan.FromMilliseconds(50)
        });

        var exception = await Assert.ThrowsAsync<ZlinkStreamException>(async () =>
            await connector.Request(new Ping("hello"))
                .PacketName("ping")
                .SubmitAsync<Pong>());

        Assert.Equal(ZlinkStreamErrorCode.RequestTimeout, exception.Error.Code);
        await server;
    }

    [Fact]
    public async Task DisconnectedSendFailsBeforeTransportWrite()
    {
        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri("tcp://127.0.0.1:1")
        });

        var exception = await Assert.ThrowsAsync<ZlinkStreamException>(async () =>
            await connector.Send(new ZlinkStreamEncodedBody(ZlinkStreamCodec.Raw, "b"u8.ToArray()))
                .PacketName("h")
                .Submit());

        Assert.Equal(ZlinkStreamErrorCode.Disconnected, exception.Error.Code);
    }

    [Fact]
    public async Task SendFrameLimitIsEnforcedBeforeTransportWrite()
    {
        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri("tcp://127.0.0.1:1"),
            MaxSendFrameSize = 6
        });

        var exception = await Assert.ThrowsAsync<ZlinkStreamException>(async () =>
            await connector.Send(new ZlinkStreamEncodedBody(ZlinkStreamCodec.Raw, "b"u8.ToArray()))
                .PacketName("h")
                .Submit());

        Assert.Equal(ZlinkStreamErrorCode.FrameTooLarge, exception.Error.Code);
    }

    [Fact]
    public void InvalidHandlerQueueCapacityIsRejected()
    {
        var exception = Assert.Throws<ZlinkStreamException>(() =>
            ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
            {
                Endpoint = new Uri("tcp://127.0.0.1:1"),
                HandlerQueueCapacity = 0
            }));

        Assert.Equal(ZlinkStreamErrorCode.ValidationFailed, exception.Error.Code);
    }

    [Fact]
    public async Task InvalidHeaderFramePublishesDecodeError()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            await WritePacketAsync(stream, "invalid-header"u8.ToArray(), "body"u8.ToArray());
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}")
        });
        var errorReceived = new TaskCompletionSource<ZlinkStreamError>(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.ErrorReceived += (error, _) =>
        {
            errorReceived.TrySetResult(error);
            return ValueTask.CompletedTask;
        };

        await connector.ConnectAsync();
        var error = await errorReceived.Task.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(ZlinkStreamErrorCode.FrameDecodeFailed, error.Code);
        await server;
    }

}

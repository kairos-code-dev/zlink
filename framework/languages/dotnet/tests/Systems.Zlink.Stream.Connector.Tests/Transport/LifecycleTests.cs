using System.Net;
using System.Net.Sockets;
using Systems.Zlink.Stream.Connector;
using Systems.Zlink.Stream.Connector.Contracts;
using Xunit;

public sealed partial class StreamConnectorTests
{
    [Fact]
    public void ReconnectDefaultMaxAttemptsIsThree()
    {
        var options = new ZlinkStreamReconnectOptions();

        Assert.Equal(3, options.MaxAttempts);
    }

    [Fact]
    public async Task HeartbeatSendsReservedControlPing()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = ZlinkStreamDefaultCodecs.Header();
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            var packet = await ReadPacketAsync(stream);
            var header = headerCodec.Decode(packet.Header);
            Assert.Equal(ZlinkStreamMessageKind.Control, header.Kind);
            Assert.Equal("$zlink.heartbeat.ping", header.Name);
            Assert.Empty(packet.Payload);
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Heartbeat = new ZlinkStreamHeartbeatOptions
            {
                Interval = TimeSpan.FromMilliseconds(20),
                Timeout = TimeSpan.FromMilliseconds(200)
            }
        });

        await connector.ConnectAsync();
        await server.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task InboundHeartbeatPingReceivesPongWithoutHeartbeatOption()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = ZlinkStreamDefaultCodecs.Header();
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            await WritePacketAsync(
                stream,
                headerCodec.Encode(new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Control,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "$zlink.heartbeat.ping",
                    ZlinkStreamMetadata.Empty)).ToArray(),
                Array.Empty<byte>());

            var packet = await ReadPacketAsync(stream);
            var header = headerCodec.Decode(packet.Header);
            Assert.Equal(ZlinkStreamMessageKind.Control, header.Kind);
            Assert.Equal("$zlink.heartbeat.pong", header.Name);
            Assert.Empty(packet.Payload);
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}")
        });

        await connector.ConnectAsync();
        await server.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task ReconnectRestoresConnectionAfterTransportClose()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var secondConnected = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var server = Task.Run(async () =>
        {
            using (var first = await listener.AcceptTcpClientAsync())
            {
                first.Close();
            }

            using var second = await listener.AcceptTcpClientAsync();
            secondConnected.SetResult();
            await Task.Delay(TimeSpan.FromMilliseconds(100));
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Reconnect = new ZlinkStreamReconnectOptions
            {
                InitialDelay = TimeSpan.FromMilliseconds(10),
                MaxDelay = TimeSpan.FromMilliseconds(20),
                BackoffFactor = 1.0,
                MaxAttempts = 3
            }
        });
        var reconnected = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var connectedCount = 0;
        connector.ConnectionStateChanged += (change, _) =>
        {
            if (change.Current == ZlinkStreamConnectionState.Connected
                && Interlocked.Increment(ref connectedCount) == 2)
            {
                reconnected.TrySetResult();
            }

            return ValueTask.CompletedTask;
        };

        await connector.ConnectAsync();
        await secondConnected.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await reconnected.Task.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.True(connector.IsConnected);
        Assert.Equal(ZlinkStreamConnectionState.Connected, connector.State);
        await server.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task HeartbeatTimeoutFailsPendingRequestsWithTimeoutCause()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await Task.Delay(TimeSpan.FromSeconds(1));
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            RequestTimeout = TimeSpan.FromSeconds(5),
            Heartbeat = new ZlinkStreamHeartbeatOptions
            {
                Interval = TimeSpan.FromMilliseconds(20),
                Timeout = TimeSpan.FromMilliseconds(80)
            }
        });

        await connector.ConnectAsync();

        var exception = await Assert.ThrowsAsync<ZlinkStreamException>(async () =>
            await connector.Request(new ZlinkStreamEncodedPayload(ZlinkStreamCodec.Raw, "b"u8.ToArray()))
                .PacketName("h")
                .Timeout(TimeSpan.FromSeconds(5))
                .SubmitAsync());

        Assert.Equal(ZlinkStreamErrorCode.Disconnected, exception.Error.Code);
        Assert.Contains("Heartbeat", exception.Error.Message, StringComparison.Ordinal);
        await server.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task ConnectAsyncWhileReconnectingFailsWhenClosed()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = Task.Run(async () =>
        {
            using var first = await listener.AcceptTcpClientAsync();
            first.Close();
            await Task.Delay(TimeSpan.FromSeconds(1));
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Reconnect = new ZlinkStreamReconnectOptions
            {
                InitialDelay = TimeSpan.FromMilliseconds(200),
                MaxDelay = TimeSpan.FromMilliseconds(200),
                BackoffFactor = 1.0,
                MaxAttempts = 3
            }
        });
        var reconnecting = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.ConnectionStateChanged += (change, _) =>
        {
            if (change.Current == ZlinkStreamConnectionState.Reconnecting)
            {
                reconnecting.TrySetResult();
            }

            return ValueTask.CompletedTask;
        };

        await connector.ConnectAsync();
        await reconnecting.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var waitingConnect = connector.ConnectAsync().AsTask();

        await connector.CloseAsync();

        await Assert.ThrowsAsync<ObjectDisposedException>(async () => await waitingConnect);
        await server.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task ReservedPacketNamesAreRejectedForUserHandlers()
    {
        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri("tcp://127.0.0.1:1")
        });

        var exception = Assert.Throws<ZlinkStreamException>(() =>
            connector.On("$zlink.user", (_, _) => ValueTask.CompletedTask));

        Assert.Equal(ZlinkStreamErrorCode.ValidationFailed, exception.Error.Code);
        await Task.CompletedTask;
    }
}

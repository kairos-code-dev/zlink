using System.Net;
using System.Net.Sockets;
using System.Buffers.Binary;
using Systems.Zlink.Stream.Connector.Contracts;
using Xunit;

public sealed partial class StreamConnectorTests
{
    [Fact]
    public void ReconnectDefaultMaxAttemptsIsThree()
    {
        var options = new ZlinkStreamReconnectOptions();

        Assert.True(options.Enabled);
        Assert.Equal(3, options.MaxAttempts);
    }

    [Fact]
    public void HeartbeatDefaultIsEnabled()
    {
        var options = new ZlinkStreamHeartbeatOptions();

        Assert.True(options.Enabled);
        Assert.Equal(TimeSpan.FromSeconds(1), options.Interval);
        Assert.Equal(TimeSpan.FromSeconds(5), options.Timeout);
    }

    [Fact]
    public async Task HeartbeatSendsReservedControlPing()
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

        await connector.Connect.Async();
        await server.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task InboundHeartbeatPingReceivesPongWhenHeartbeatDisabled()
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
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false }
        });

        await connector.Connect.Async();
        await server.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task SessionClosingPublishesServerDrainReasonAfterDisconnectedState()
    {
        const int repetitions = 32;
        for (var attempt = 0; attempt < repetitions; attempt++)
            await AssertSessionClosingOrderAsync();
    }

    private static async Task AssertSessionClosingOrderAsync()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = new ZlinkStreamHeaderCodec();
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            var payload = new byte[4];
            payload[0] = 1;
            payload[1] = 4;
            BinaryPrimitives.WriteUInt16BigEndian(payload.AsSpan(2, 2), 0);
            await WritePacketAsync(
                stream,
                headerCodec.Encode(new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Control,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    ZlinkStreamSessionClosingCodec.ControlName,
                    ZlinkStreamMetadata.Empty)).ToArray(),
                payload);
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = new ZlinkStreamReconnectOptions { Enabled = false }
        });
        var order = new List<string>();
        var disconnected = new TaskCompletionSource<ZlinkStreamDisconnected>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        connector.ConnectionStateChanged += (change, _) =>
        {
            lock (order) order.Add($"state:{change.Current}");
            return ValueTask.CompletedTask;
        };
        connector.Disconnected += (closed, _) =>
        {
            lock (order) order.Add($"disconnected:{closed.CloseReason}");
            disconnected.TrySetResult(closed);
            return ValueTask.CompletedTask;
        };

        await connector.Connect.Async();
        var closed = await disconnected.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await server.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(ZlinkStreamCloseReason.ServerDrain, closed.CloseReason);
        Assert.Equal(
            new[] { "state:Connecting", "state:Connected", "state:Disconnected", "disconnected:ServerDrain" },
            order);
    }

    [Fact]
    public async Task ImmediateConnectedCallbackCanAwaitCloseWithoutWaitingForItsConnectWork()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = ObserveClientCloseAsync(listener);

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = new ZlinkStreamReconnectOptions { Enabled = false }
        });
        var callbackCompleted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.ConnectionStateChanged += async (change, _) =>
        {
            if (change.Current != ZlinkStreamConnectionState.Connected) return;

            await connector.Close.Async();
            callbackCompleted.TrySetResult();
        };

        await Assert.ThrowsAsync<ObjectDisposedException>(async () =>
            await connector.Connect.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5)));
        await callbackCompleted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await server.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(ZlinkStreamConnectionState.Closed, connector.State);
    }

    [Fact]
    public async Task ImmediateDisconnectedCallbackCanAwaitCloseWithoutWaitingForItsReceiveWork()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = new ZlinkStreamHeaderCodec();
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            var payload = new byte[] { 1, 4, 0, 0 };
            await WritePacketAsync(
                stream,
                headerCodec.Encode(new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Control,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    ZlinkStreamSessionClosingCodec.ControlName,
                    ZlinkStreamMetadata.Empty)).ToArray(),
                payload);
            var buffer = new byte[1];
            Assert.Equal(0, await stream.ReadAsync(buffer, 0, buffer.Length));
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = new ZlinkStreamReconnectOptions { Enabled = false }
        });
        var callbackCompleted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.Disconnected += async (_, _) =>
        {
            await connector.Close.Async();
            callbackCompleted.TrySetResult();
        };

        await connector.Connect.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        await callbackCompleted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await server.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(ZlinkStreamConnectionState.Closed, connector.State);
    }

    [Fact]
    public async Task ImmediateReceiveErrorCallbackCanAwaitCloseWithoutWaitingForItsReceiveWork()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            await WritePacketAsync(stream, "invalid-header"u8.ToArray(), "payload"u8.ToArray());
            var buffer = new byte[1];
            Assert.Equal(0, await stream.ReadAsync(buffer, 0, buffer.Length));
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = new ZlinkStreamReconnectOptions { Enabled = false }
        });
        var callbackCompleted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.ErrorReceived += async (error, _) =>
        {
            if (error.Code != ZlinkStreamErrorCode.FrameDecodeFailed) return;

            await connector.Close.Async();
            callbackCompleted.TrySetResult();
        };

        await connector.Connect.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        await callbackCompleted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await server.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(ZlinkStreamConnectionState.Closed, connector.State);
    }

    [Fact]
    public async Task ImmediateReconnectErrorCallbackCanAwaitCloseWithoutWaitingForItsConnectWork()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            tcp.Close();
            listener.Stop();
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = new ZlinkStreamReconnectOptions
            {
                InitialDelay = TimeSpan.FromMilliseconds(10),
                MaxDelay = TimeSpan.FromMilliseconds(10),
                BackoffFactor = 1.0,
                MaxAttempts = 3
            }
        });
        var callbackCompleted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.ErrorReceived += async (_, _) =>
        {
            await connector.Close.Async();
            callbackCompleted.TrySetResult();
        };

        await connector.Connect.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        await server.WaitAsync(TimeSpan.FromSeconds(5));
        await callbackCompleted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(ZlinkStreamConnectionState.Closed, connector.State);
    }

    [Fact]
    public async Task ImmediateTypedPacketCallbackCanAwaitCloseWithoutWaitingForReceiveWorker()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = SendFrameAndObserveClientCloseAsync(
            listener,
            ZlinkStreamMessageKind.Send,
            "close-from-handler",
            Array.Empty<byte>());

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = new ZlinkStreamReconnectOptions { Enabled = false }
        });
        var callbackCompleted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var subscription = connector.On("close-from-handler", async (_, _) =>
        {
            await connector.Close.Async();
            callbackCompleted.TrySetResult();
        });

        await connector.Connect.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        await callbackCompleted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await server.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(ZlinkStreamConnectionState.Closed, connector.State);
    }

    [Fact]
    public async Task ImmediateUnsolicitedErrorCallbackCanAwaitCloseWithoutWaitingForReceiveWorker()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = SendFrameAndObserveClientCloseAsync(
            listener,
            ZlinkStreamMessageKind.Error,
            "remote-error",
            "{\"message\":\"close requested\"}"u8.ToArray());

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = new ZlinkStreamReconnectOptions { Enabled = false }
        });
        var callbackCompleted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.ErrorReceived += async (error, _) =>
        {
            if (error.Code != ZlinkStreamErrorCode.RemoteError) return;

            await connector.Close.Async();
            callbackCompleted.TrySetResult();
        };

        await connector.Connect.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        await callbackCompleted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await server.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(ZlinkStreamConnectionState.Closed, connector.State);
    }

    [Fact]
    public async Task ImmediateUserCallbackFailureHandlerCanAwaitCloseWithoutWaitingForReceiveWorker()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = SendFrameAndObserveClientCloseAsync(
            listener,
            ZlinkStreamMessageKind.Send,
            "throw-from-handler",
            Array.Empty<byte>());

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = new ZlinkStreamReconnectOptions { Enabled = false }
        });
        var callbackCompleted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.ErrorReceived += async (error, _) =>
        {
            if (error.Code != ZlinkStreamErrorCode.UserCallbackFailed) return;

            await connector.Close.Async();
            callbackCompleted.TrySetResult();
        };
        using var subscription = connector.On("throw-from-handler", (_, _) =>
            throw new InvalidOperationException("expected callback failure"));

        await connector.Connect.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        await callbackCompleted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await server.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(ZlinkStreamConnectionState.Closed, connector.State);
    }

    [Fact]
    public async Task DetachedChildDisposeAfterCallbackReturnAwaitsWorkerTerminationAndSharedFinalizer()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var clientCloseObserved = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            var headerCodec = new ZlinkStreamHeaderCodec();
            foreach (var name in new[] { "spawn-dispose", "block-worker" })
                await WritePacketAsync(
                    stream,
                    headerCodec.Encode(new ZlinkStreamHeader(
                        ZlinkStreamMessageKind.Send,
                        ZlinkStreamCodec.Raw,
                        ZlinkStreamHeaderFlags.None,
                        null,
                        name,
                        ZlinkStreamMetadata.Empty)).ToArray(),
                    Array.Empty<byte>());
            var buffer = new byte[1];
            Assert.Equal(0, await stream.ReadAsync(buffer, 0, buffer.Length));
            clientCloseObserved.TrySetResult();
        });

        var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = new ZlinkStreamReconnectOptions { Enabled = false }
        });
        var releaseChild = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseWorker = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var workerBlocked = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var childDisposeCreated = new TaskCompletionSource<Task>(TaskCreationOptions.RunContinuationsAsynchronously);
        using var spawnSubscription = connector.On("spawn-dispose", (_, _) =>
        {
            var childDispose = Task.Run(async () =>
            {
                await releaseChild.Task;
                await connector.DisposeAsync();
            });
            childDisposeCreated.TrySetResult(childDispose);
            return ValueTask.CompletedTask;
        });
        using var blockSubscription = connector.On("block-worker", async (_, _) =>
        {
            workerBlocked.TrySetResult();
            await releaseWorker.Task;
        });

        await connector.Connect.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        var childDispose = await childDisposeCreated.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await workerBlocked.Task.WaitAsync(TimeSpan.FromSeconds(5));
        releaseChild.TrySetResult();
        await clientCloseObserved.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var externalDispose = connector.DisposeAsync().AsTask();

        Assert.False(childDispose.IsCompleted);
        Assert.False(externalDispose.IsCompleted);

        releaseWorker.TrySetResult();
        await childDispose.WaitAsync(TimeSpan.FromSeconds(5));
        await externalDispose.WaitAsync(TimeSpan.FromSeconds(5));
        await server.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task AwaitedChildCloseDuringCallbackRetainsReentrantPermit()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = SendFrameAndObserveClientCloseAsync(
            listener,
            ZlinkStreamMessageKind.Send,
            "await-child-close",
            Array.Empty<byte>());

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = new ZlinkStreamReconnectOptions { Enabled = false }
        });
        var callbackCompleted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var subscription = connector.On("await-child-close", async (_, _) =>
        {
            await Task.Run(async () => await connector.Close.Async());
            callbackCompleted.TrySetResult();
        });

        await connector.Connect.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        await callbackCompleted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await server.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(ZlinkStreamConnectionState.Closed, connector.State);
    }

    [Fact]
    public async Task CallbackPermitDoesNotExcludeAnotherConnectorWorker()
    {
        using var listenerA = new TcpListener(IPAddress.Loopback, 0);
        using var listenerB = new TcpListener(IPAddress.Loopback, 0);
        listenerA.Start();
        listenerB.Start();
        var endpointA = (IPEndPoint)listenerA.LocalEndpoint;
        var endpointB = (IPEndPoint)listenerB.LocalEndpoint;
        var serverA = SendSingleFrameAsync(listenerA, "close-other");
        var serverB = SendFrameAndObserveClientCloseAsync(
            listenerB,
            ZlinkStreamMessageKind.Send,
            "block-other",
            Array.Empty<byte>());

        await using var connectorA = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpointA.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = new ZlinkStreamReconnectOptions { Enabled = false }
        });
        await using var connectorB = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpointB.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = new ZlinkStreamReconnectOptions { Enabled = false }
        });
        var releaseB = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var workerBBlocked = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var closeBStarted = new TaskCompletionSource<Task>(TaskCreationOptions.RunContinuationsAsynchronously);
        using var subscriptionB = connectorB.On("block-other", async (_, _) =>
        {
            workerBBlocked.TrySetResult();
            await releaseB.Task;
        });
        using var subscriptionA = connectorA.On("close-other", async (_, _) =>
        {
            var closeB = connectorB.Close.Async().AsTask();
            closeBStarted.TrySetResult(closeB);
            await closeB;
        });

        await connectorB.Connect.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        await workerBBlocked.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await connectorA.Connect.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        var closeB = await closeBStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await serverB.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.False(closeB.IsCompleted);

        releaseB.TrySetResult();
        await closeB.WaitAsync(TimeSpan.FromSeconds(5));
        await serverA.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task CanceledClosePublishesTerminalStateWhileTerminationContinues()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = SendFrameAndObserveClientCloseAsync(
            listener,
            ZlinkStreamMessageKind.Send,
            "block-canceled-close",
            Array.Empty<byte>());

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = new ZlinkStreamReconnectOptions { Enabled = false }
        });
        var releaseWorker = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var workerBlocked = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var disconnected = new TaskCompletionSource<ZlinkStreamDisconnected>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        connector.Disconnected += (closed, _) =>
        {
            disconnected.TrySetResult(closed);
            return ValueTask.CompletedTask;
        };
        using var subscription = connector.On("block-canceled-close", async (_, _) =>
        {
            workerBlocked.TrySetResult();
            await releaseWorker.Task;
        });

        await connector.Connect.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        await workerBlocked.Task.WaitAsync(TimeSpan.FromSeconds(5));
        using var canceled = new CancellationTokenSource();
        canceled.Cancel();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () =>
            await connector.Close.Async(canceled.Token));
        await server.WaitAsync(TimeSpan.FromSeconds(5));
        var closed = await disconnected.Task.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(ZlinkStreamConnectionState.Closed, connector.State);
        Assert.Equal(ZlinkStreamCloseReason.ClientClose, closed.CloseReason);
        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () =>
            await connector.Close.Async(canceled.Token));

        releaseWorker.TrySetResult();
        await connector.Close.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task DisposeInsideCallbackIsRejectedWithoutStartingFinalization()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = SendFrameAndObserveClientCloseAsync(
            listener,
            ZlinkStreamMessageKind.Send,
            "reject-dispose",
            Array.Empty<byte>());

        var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = new ZlinkStreamReconnectOptions { Enabled = false }
        });
        var rejected = new TaskCompletionSource<InvalidOperationException>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        using var subscription = connector.On("reject-dispose", async (_, _) =>
        {
            try
            {
                await connector.DisposeAsync();
            }
            catch (InvalidOperationException ex)
            {
                rejected.TrySetResult(ex);
            }
        });

        await connector.Connect.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        var exception = await rejected.Task.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Contains("Close.Async", exception.Message, StringComparison.Ordinal);
        Assert.Equal(ZlinkStreamConnectionState.Connected, connector.State);

        await connector.DisposeAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        await server.WaitAsync(TimeSpan.FromSeconds(5));
        await connector.DisposeAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task ManualConnectedCallbackCanAwaitCloseFromDispatch()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var server = ObserveClientCloseAsync(listener);

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Manual,
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = new ZlinkStreamReconnectOptions { Enabled = false }
        });
        var callbackCompleted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.ConnectionStateChanged += async (change, _) =>
        {
            if (change.Current != ZlinkStreamConnectionState.Connected) return;

            await connector.Close.Async();
            callbackCompleted.TrySetResult();
        };

        await connector.Connect.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        await connector.Dispatch.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        await callbackCompleted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await server.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(ZlinkStreamConnectionState.Closed, connector.State);
    }

    [Fact]
    public async Task ManualDisconnectedCallbackCanAwaitCloseFromDispatch()
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
                    ZlinkStreamMessageKind.Control,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    ZlinkStreamSessionClosingCodec.ControlName,
                    ZlinkStreamMetadata.Empty)).ToArray(),
                new byte[] { 1, 4, 0, 0 });
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Manual,
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = new ZlinkStreamReconnectOptions { Enabled = false }
        });
        var callbackCompleted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.Disconnected += async (_, _) =>
        {
            await connector.Close.Async();
            callbackCompleted.TrySetResult();
        };

        await connector.Connect.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        await server.WaitAsync(TimeSpan.FromSeconds(5));
        await WaitUntilAsync(
            () => connector.State == ZlinkStreamConnectionState.Disconnected,
            TimeSpan.FromSeconds(5));
        await connector.Dispatch.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        await callbackCompleted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(ZlinkStreamConnectionState.Closed, connector.State);
    }

    private static async Task ObserveClientCloseAsync(TcpListener listener)
    {
        using var tcp = await listener.AcceptTcpClientAsync();
        await using var stream = tcp.GetStream();
        var buffer = new byte[1];
        Assert.Equal(0, await stream.ReadAsync(buffer, 0, buffer.Length));
    }

    private static async Task SendFrameAndObserveClientCloseAsync(
        TcpListener listener,
        ZlinkStreamMessageKind kind,
        string name,
        byte[] payload)
    {
        using var tcp = await listener.AcceptTcpClientAsync();
        await using var stream = tcp.GetStream();
        var headerCodec = new ZlinkStreamHeaderCodec();
        await WritePacketAsync(
            stream,
            headerCodec.Encode(new ZlinkStreamHeader(
                kind,
                kind == ZlinkStreamMessageKind.Error ? ZlinkStreamCodec.Json : ZlinkStreamCodec.Raw,
                ZlinkStreamHeaderFlags.None,
                null,
                name,
                ZlinkStreamMetadata.Empty)).ToArray(),
            payload);
        var buffer = new byte[1];
        Assert.Equal(0, await stream.ReadAsync(buffer, 0, buffer.Length));
    }

    private static async Task SendSingleFrameAsync(TcpListener listener, string name)
    {
        using var tcp = await listener.AcceptTcpClientAsync();
        await using var stream = tcp.GetStream();
        var headerCodec = new ZlinkStreamHeaderCodec();
        await WritePacketAsync(
            stream,
            headerCodec.Encode(new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Send,
                ZlinkStreamCodec.Raw,
                ZlinkStreamHeaderFlags.None,
                null,
                name,
                ZlinkStreamMetadata.Empty)).ToArray(),
            Array.Empty<byte>());
    }

    [Fact]
    public async Task ReconnectRestoresConnectionAfterTransportClose()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var secondConnected = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var reconnectedObserved = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var server = Task.Run(async () =>
        {
            using (var first = await listener.AcceptTcpClientAsync())
            {
                first.Close();
            }

            using var second = await listener.AcceptTcpClientAsync();
            secondConnected.SetResult();
            await reconnectedObserved.Task.WaitAsync(TimeSpan.FromSeconds(5));
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            Reconnect = new ZlinkStreamReconnectOptions
            {
                InitialDelay = TimeSpan.FromMilliseconds(10),
                MaxDelay = TimeSpan.FromMilliseconds(20),
                BackoffFactor = 1.0,
                MaxAttempts = 3
            }
        });
        await connector.Connect.Async();
        await secondConnected.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await WaitUntilAsync(
            () => connector.State == ZlinkStreamConnectionState.Connected,
            TimeSpan.FromSeconds(5));
        reconnectedObserved.SetResult();

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

        await connector.Connect.Async();

        var exception = await Assert.ThrowsAsync<ZlinkStreamException>(async () =>
            await connector.Request(new ZlinkStreamEncodedPayload(ZlinkStreamCodec.Raw, "b"u8.ToArray()))
                .PacketName("h")
                .Timeout(TimeSpan.FromSeconds(5))
                .Async());

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
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            Reconnect = new ZlinkStreamReconnectOptions
            {
                InitialDelay = TimeSpan.FromSeconds(10),
                MaxDelay = TimeSpan.FromSeconds(10),
                BackoffFactor = 1.0,
                MaxAttempts = 3
            }
        });
        var reconnecting = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.ConnectionStateChanged += (change, _) =>
        {
            if (change.Current == ZlinkStreamConnectionState.Reconnecting) reconnecting.TrySetResult();

            return ValueTask.CompletedTask;
        };

        await connector.Connect.Async();
        await reconnecting.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var waitingConnect = connector.Connect.Async().AsTask();

        await connector.Close.Async();

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

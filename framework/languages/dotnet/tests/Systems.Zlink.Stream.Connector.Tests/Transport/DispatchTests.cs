using System.Net;
using System.Net.Sockets;
using Systems.Zlink.Stream.Connector.Contracts;
using Xunit;

public sealed partial class StreamConnectorTests
{
    [Fact]
    public async Task ManualDispatchRunsHandlerOnDispatchCaller()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = new ZlinkStreamHeaderCodec();
        var received = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var packetWritten = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            await WritePacketAsync(
                stream,
                headerCodec.Encode(new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "manual",
                    ZlinkStreamMetadata.Empty)).ToArray(),
                "payload"u8.ToArray());
            packetWritten.SetResult();
            await received.Task.WaitAsync(TimeSpan.FromSeconds(15));
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Heartbeat = DisabledHeartbeat()
        });
        var receivedThread = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.On("manual", (_, _) =>
        {
            receivedThread.SetResult(Environment.CurrentManagedThreadId);
            received.TrySetResult();
            return ValueTask.CompletedTask;
        });

        await connector.Connect.Async();
        await packetWritten.Task.WaitAsync(TimeSpan.FromSeconds(15));
        await WaitUntilAsync(
            () => connector.PendingDispatchCount > 0,
            TimeSpan.FromSeconds(15));

        Assert.False(receivedThread.Task.IsCompleted);
        var dispatchThread = Environment.CurrentManagedThreadId;
        await connector.Dispatch.Async();

        Assert.Equal(dispatchThread, await receivedThread.Task.WaitAsync(TimeSpan.FromSeconds(5)));
        Assert.Equal(0, connector.PendingDispatchCount);
        await server;
    }

    [Fact]
    public async Task ReceivedMessagesDropNewestEntryAndReportOverflowAtConfiguredLimit()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = new ZlinkStreamHeaderCodec();
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            for (var index = 1; index <= 3; index++)
                await WritePacketAsync(
                    stream,
                    headerCodec.Encode(new ZlinkStreamHeader(
                        ZlinkStreamMessageKind.Send,
                        ZlinkStreamCodec.Raw,
                        ZlinkStreamHeaderFlags.None,
                        null,
                        "buffered",
                        ZlinkStreamMetadata.Empty)).ToArray(),
                    [(byte)index]);
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Heartbeat = DisabledHeartbeat(),
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            MaxReceivedMessages = 2
        });
        var dropped = new TaskCompletionSource<ZlinkStreamError>(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.ErrorReceived += (error, _) =>
        {
            if (error.Code == ZlinkStreamErrorCode.ReceivedMessageDropped) dropped.TrySetResult(error);
            return ValueTask.CompletedTask;
        };
        await connector.Connect.Async();
        await server;
        await WaitUntilAsync(
            () => connector.ReceivedCount("buffered") == 2,
            TimeSpan.FromSeconds(15));
        var dropError = await dropped.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var first = await connector.WaitFor("buffered")
            .Where(message => message.Payload.Payload.Span[0] == 1)
            .Timeout(TimeSpan.FromSeconds(1))
            .Async();
        var second = await connector.WaitFor("buffered")
            .Timeout(TimeSpan.FromSeconds(1))
            .Async();

        Assert.Equal(ZlinkStreamErrorCode.ReceivedMessageDropped, dropError.Code);
        Assert.Equal(1, first.Payload.Payload.Span[0]);
        Assert.Equal(2, second.Payload.Payload.Span[0]);
        Assert.Equal(0, connector.ReceivedCount("buffered"));
    }

    [Fact]
    public async Task ManualDispatchCallbackQueueDropsOldestCallbacksAtConfiguredLimit()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = new ZlinkStreamHeaderCodec();
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            for (var index = 1; index <= 3; index++)
                await WritePacketAsync(
                    stream,
                    headerCodec.Encode(new ZlinkStreamHeader(
                        ZlinkStreamMessageKind.Send,
                        ZlinkStreamCodec.Raw,
                        ZlinkStreamHeaderFlags.None,
                        null,
                        "queued",
                        ZlinkStreamMetadata.Empty)).ToArray(),
                    [(byte)index]);
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Heartbeat = DisabledHeartbeat(),
            MaxPendingDispatchCallbacks = 2
        });
        var handled = new List<byte>();
        connector.On("queued", (message, _) =>
        {
            handled.Add(message.Payload.Payload.Span[0]);
            return ValueTask.CompletedTask;
        });

        await connector.Connect.Async();
        await server;
        await WaitUntilAsync(
            () => connector.PendingDispatchCount == 2,
            TimeSpan.FromSeconds(15));
        await connector.Dispatch.Async();

        Assert.Equal([2, 3], handled);
        Assert.Equal(0, connector.PendingDispatchCount);
    }

    [Fact]
    public async Task ManualRequestCallbackAdmission_Is_Bounded_And_Never_Falls_Back_To_A_Background_Thread()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = new ZlinkStreamHeaderCodec();
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            var request = await ReadPacketAsync(stream);
            var requestHeader = headerCodec.Decode(request.Header);
            var responseHeader = new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Response,
                ZlinkStreamCodec.Raw,
                ZlinkStreamHeaderFlags.HasRequestSeq,
                requestHeader.RequestSeq,
                requestHeader.Name,
                ZlinkStreamMetadata.Empty);
            await WritePacketAsync(
                stream,
                headerCodec.Encode(responseHeader).ToArray(),
                [1]);
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Heartbeat = DisabledHeartbeat(),
            MaxPendingDispatchCallbacks = 1
        });
        var callbackThread = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        await connector.Connect.Async();

        connector.Request(new ZlinkStreamEncodedPayload(ZlinkStreamCodec.Raw, new byte[] { 1 }))
            .PacketName("request.one")
            .Submit((ZlinkStreamResult<ZlinkStreamEncodedPayload> result) =>
            {
                Assert.True(result.IsSuccess);
                callbackThread.SetResult(Environment.CurrentManagedThreadId);
            });

        var full = Assert.Throws<ZlinkStreamException>(() =>
            connector.Request(new ZlinkStreamEncodedPayload(ZlinkStreamCodec.Raw, new byte[] { 2 }))
                .PacketName("request.two")
                .Submit((ZlinkStreamResult<ZlinkStreamEncodedPayload> _) => { }));
        Assert.Equal(ZlinkStreamErrorCode.SendFailed, full.Error.Code);
        Assert.Contains("queue is full", full.Error.Message, StringComparison.Ordinal);

        await server;
        await WaitUntilAsync(
            () => connector.PendingDispatchCount == 1,
            TimeSpan.FromSeconds(15));
        Assert.False(callbackThread.Task.IsCompleted);

        var dispatchThread = Environment.CurrentManagedThreadId;
        await connector.Dispatch.Async();

        Assert.Equal(dispatchThread, await callbackThread.Task.WaitAsync(TimeSpan.FromSeconds(5)));
        Assert.Equal(0, connector.PendingDispatchCount);
    }

    [Fact]
    public async Task Cancelled_Manual_Dispatch_Does_Not_Remove_A_Reserved_Request_Callback()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = new ZlinkStreamHeaderCodec();
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            var request = await ReadPacketAsync(stream);
            var requestHeader = headerCodec.Decode(request.Header);
            await WritePacketAsync(
                stream,
                headerCodec.Encode(new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Response,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    requestHeader.RequestSeq,
                    requestHeader.Name,
                    ZlinkStreamMetadata.Empty)).ToArray(),
                [1]);
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Heartbeat = DisabledHeartbeat(),
            MaxPendingDispatchCallbacks = 1
        });
        var callbackThread = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        await connector.Connect.Async();
        connector.Request(new ZlinkStreamEncodedPayload(ZlinkStreamCodec.Raw, new byte[] { 1 }))
            .PacketName("request.cancel")
            .Submit((ZlinkStreamResult<ZlinkStreamEncodedPayload> result) =>
            {
                Assert.True(result.IsSuccess);
                callbackThread.SetResult(Environment.CurrentManagedThreadId);
            });

        await server;
        await WaitUntilAsync(() => connector.PendingDispatchCount == 1, TimeSpan.FromSeconds(15));
        using var canceled = new CancellationTokenSource();
        canceled.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            () => connector.Dispatch.Async(canceled.Token).AsTask());

        Assert.Equal(1, connector.PendingDispatchCount);
        Assert.False(callbackThread.Task.IsCompleted);
        var dispatchThread = Environment.CurrentManagedThreadId;
        await connector.Dispatch.Async();
        Assert.Equal(dispatchThread, await callbackThread.Task.WaitAsync(TimeSpan.FromSeconds(5)));
    }

    [Fact]
    public async Task Close_Queues_The_Reserved_Request_Failure_Until_Manual_Dispatch()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var requestRead = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            _ = await ReadPacketAsync(stream);
            requestRead.SetResult();
            var buffer = new byte[1];
            while (await stream.ReadAsync(buffer) != 0)
            {
            }
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Heartbeat = DisabledHeartbeat(),
            MaxPendingDispatchCallbacks = 1
        });
        var callback = new TaskCompletionSource<(int Thread, ZlinkStreamResult Result)>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        await connector.Connect.Async();
        connector.Request(new ZlinkStreamEncodedPayload(ZlinkStreamCodec.Raw, new byte[] { 1 }))
            .PacketName("request.close")
            .Submit(result => callback.SetResult((Environment.CurrentManagedThreadId, result)));
        await requestRead.Task.WaitAsync(TimeSpan.FromSeconds(15));

        await connector.Close.Async();
        await server.WaitAsync(TimeSpan.FromSeconds(15));
        await WaitUntilAsync(() => connector.PendingDispatchCount == 1, TimeSpan.FromSeconds(15));
        Assert.False(callback.Task.IsCompleted);

        var dispatchThread = Environment.CurrentManagedThreadId;
        await connector.Dispatch.Async();
        var completion = await callback.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(dispatchThread, completion.Thread);
        Assert.False(completion.Result.IsSuccess);
        Assert.Equal(ZlinkStreamErrorCode.Disconnected, completion.Result.Error?.Code);
    }

    [Fact]
    public async Task Dispose_Drains_Blocked_Manual_Request_And_Does_Not_Strand_Its_Callback()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var requestRead = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            _ = await ReadPacketAsync(stream);
            requestRead.SetResult();
            var buffer = new byte[1];
            while (await stream.ReadAsync(buffer) != 0)
            {
            }
        });

        var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Heartbeat = DisabledHeartbeat(),
            DispatchMode = ZlinkStreamDispatchMode.Manual,
            MaxPendingDispatchCallbacks = 1
        });
        var callbackCount = 0;
        await connector.Connect.Async();
        connector.Request(new ZlinkStreamEncodedPayload(ZlinkStreamCodec.Raw, new byte[] { 1 }))
            .PacketName("request.dispose")
            .Submit((ZlinkStreamResult _) => Interlocked.Increment(ref callbackCount));
        await requestRead.Task.WaitAsync(TimeSpan.FromSeconds(15));

        var firstDispose = connector.DisposeAsync().AsTask();
        var secondDispose = connector.DisposeAsync().AsTask();
        Assert.Same(firstDispose, secondDispose);
        await Task.WhenAll(firstDispose, secondDispose).WaitAsync(TimeSpan.FromSeconds(15));
        await server.WaitAsync(TimeSpan.FromSeconds(15));

        Assert.Equal(0, Volatile.Read(ref callbackCount));
        Assert.Equal(0, connector.PendingDispatchCount);
        await Task.Delay(TimeSpan.FromMilliseconds(50));
        Assert.Equal(0, Volatile.Read(ref callbackCount));
    }

    [Fact]
    public async Task ImmediateDispatchRunsHandlerWithoutManualDispatch()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = new ZlinkStreamHeaderCodec();
        var received = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var packetWritten = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            await WritePacketAsync(
                stream,
                headerCodec.Encode(new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "immediate",
                    ZlinkStreamMetadata.Empty)).ToArray(),
                Array.Empty<byte>());
            packetWritten.SetResult();
            await received.Task.WaitAsync(TimeSpan.FromSeconds(15));
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Heartbeat = DisabledHeartbeat(),
            DispatchMode = ZlinkStreamDispatchMode.Immediate
        });
        connector.On("immediate", (_, _) =>
        {
            received.SetResult();
            return ValueTask.CompletedTask;
        });

        await connector.Connect.Async();
        await packetWritten.Task.WaitAsync(TimeSpan.FromSeconds(15));
        await received.Task.WaitAsync(TimeSpan.FromSeconds(15));

        Assert.Equal(0, connector.PendingDispatchCount);
        await server;
    }
}

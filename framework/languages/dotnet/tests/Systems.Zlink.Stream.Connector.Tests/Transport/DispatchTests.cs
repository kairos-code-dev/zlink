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
        var headerCodec = ZlinkStreamDefaultCodecFactory.Header();
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
    public async Task ReceivedMessagesDropOldestEntriesAtConfiguredLimit()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = ZlinkStreamDefaultCodecFactory.Header();
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
            MaxReceivedMessages = 2
        });
        await connector.Connect.Async();
        await server;
        await WaitUntilAsync(
            () => connector.ReceivedCount("buffered") == 2,
            TimeSpan.FromSeconds(15));

        var second = await connector.WaitFor("buffered")
            .Where(message => message.Payload.Payload.Span[0] == 2)
            .Timeout(TimeSpan.FromSeconds(1))
            .Async();
        var third = await connector.WaitFor("buffered")
            .Timeout(TimeSpan.FromSeconds(1))
            .Async();

        Assert.Equal(2, second.Payload.Payload.Span[0]);
        Assert.Equal(3, third.Payload.Payload.Span[0]);
        Assert.Equal(0, connector.ReceivedCount("buffered"));
    }

    [Fact]
    public async Task ManualDispatchCallbackQueueDropsOldestCallbacksAtConfiguredLimit()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = ZlinkStreamDefaultCodecFactory.Header();
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
    public async Task ManualDispatchOverflowStillDeliversRequestCallbacks()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = ZlinkStreamDefaultCodecFactory.Header();
        var server = Task.Run(async () =>
        {
            using var tcp = await listener.AcceptTcpClientAsync();
            await using var stream = tcp.GetStream();
            for (var index = 1; index <= 2; index++)
            {
                var request = await ReadPacketAsync(stream);
                var requestHeader = headerCodec.Decode(request.Header);
                var responseHeader = new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Response,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    requestHeader.RequestSeq,
                    $"reply.{index}",
                    ZlinkStreamMetadata.Empty);
                await WritePacketAsync(
                    stream,
                    headerCodec.Encode(responseHeader).ToArray(),
                    [(byte)index]);
            }
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            Heartbeat = DisabledHeartbeat(),
            MaxPendingDispatchCallbacks = 1
        });
        var results = new List<byte>();
        var resultsGate = new object();
        await connector.Connect.Async();

        connector.Request(new ZlinkStreamEncodedPayload(ZlinkStreamCodec.Raw, new byte[] { 1 }))
            .PacketName("request.one")
            .Submit((ZlinkStreamResult<ZlinkStreamEncodedPayload> result) =>
            {
                if (result.IsSuccess)
                    lock (resultsGate)
                    {
                        results.Add(1);
                    }
            });
        connector.Request(new ZlinkStreamEncodedPayload(ZlinkStreamCodec.Raw, new byte[] { 2 }))
            .PacketName("request.two")
            .Submit((ZlinkStreamResult<ZlinkStreamEncodedPayload> result) =>
            {
                if (result.IsSuccess)
                    lock (resultsGate)
                    {
                        results.Add(2);
                    }
            });

        await server;
        await WaitUntilAsync(
            () => connector.PendingDispatchCount == 1 && ResultCount() >= 1,
            TimeSpan.FromSeconds(15));
        await connector.Dispatch.Async();
        await WaitUntilAsync(
            () => ResultCount() == 2,
            TimeSpan.FromSeconds(15));

        lock (resultsGate)
        {
            Assert.Equal([1, 2], results.Order().ToArray());
        }

        Assert.Equal(0, connector.PendingDispatchCount);

        int ResultCount()
        {
            lock (resultsGate)
            {
                return results.Count;
            }
        }
    }

    [Fact]
    public async Task ImmediateDispatchRunsHandlerWithoutManualDispatch()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var headerCodec = ZlinkStreamDefaultCodecFactory.Header();
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
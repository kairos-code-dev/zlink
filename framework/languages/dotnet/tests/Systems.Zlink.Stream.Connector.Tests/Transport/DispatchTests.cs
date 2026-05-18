using System.Net;
using System.Net.Sockets;
using Systems.Zlink.Stream.Connector;
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

        await connector.ConnectAsync();
        await packetWritten.Task.WaitAsync(TimeSpan.FromSeconds(15));
        await WaitUntilAsync(
            () => connector.PendingDispatchCount > 0,
            TimeSpan.FromSeconds(15));

        Assert.False(receivedThread.Task.IsCompleted);
        var dispatchThread = Environment.CurrentManagedThreadId;
        await connector.DispatchAsync();

        Assert.Equal(dispatchThread, await receivedThread.Task.WaitAsync(TimeSpan.FromSeconds(5)));
        Assert.Equal(0, connector.PendingDispatchCount);
        await server;
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

        await connector.ConnectAsync();
        await packetWritten.Task.WaitAsync(TimeSpan.FromSeconds(15));
        await received.Task.WaitAsync(TimeSpan.FromSeconds(15));

        Assert.Equal(0, connector.PendingDispatchCount);
        await server;
    }
}

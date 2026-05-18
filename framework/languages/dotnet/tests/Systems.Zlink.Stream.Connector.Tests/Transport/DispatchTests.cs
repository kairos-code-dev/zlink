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
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}")
        });
        var received = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.On("manual", (_, _) =>
        {
            received.SetResult(Environment.CurrentManagedThreadId);
            return ValueTask.CompletedTask;
        });

        await connector.ConnectAsync();
        await WaitUntilAsync(
            () => connector.PendingDispatchCount > 0,
            TimeSpan.FromSeconds(5));

        Assert.False(received.Task.IsCompleted);
        var dispatchThread = Environment.CurrentManagedThreadId;
        await connector.DispatchAsync();

        Assert.Equal(dispatchThread, await received.Task.WaitAsync(TimeSpan.FromSeconds(1)));
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
        });

        await using var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri($"tcp://127.0.0.1:{endpoint.Port}"),
            DispatchMode = ZlinkStreamDispatchMode.Immediate
        });
        var received = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        connector.On("immediate", (_, _) =>
        {
            received.SetResult();
            return ValueTask.CompletedTask;
        });

        await connector.ConnectAsync();
        await received.Task.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(0, connector.PendingDispatchCount);
        await server;
    }
}

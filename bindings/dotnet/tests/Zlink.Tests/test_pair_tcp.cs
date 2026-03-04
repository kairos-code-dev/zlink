using System.Text;
using System.Threading;
using Xunit;

namespace Zlink.Tests;

public sealed class test_pair_tcp
{
    [Theory]
    [InlineData("tcp")]
    [InlineData("ipc")]
    [InlineData("inproc")]
    public void pair_roundtrip(string transport)
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;
        if (!CoreTestSupport.IsTransportSupported(transport))
            return;

        using var ctx = new Context();
        using var sb = new Socket(ctx, SocketType.Pair);
        using var sc = new Socket(ctx, SocketType.Pair);

        string endpoint = CoreTestSupport.NewEndpoint(transport, "pair");
        sb.Bind(endpoint);
        sc.Connect(endpoint);
        Thread.Sleep(50);

        CoreTestSupport.SendBytesWithRetry(sc, "ping"u8, SendFlags.None, 2000);
        Assert.Equal("ping", CoreTestSupport.ReceiveStringWithTimeout(sb, 64, 2000));

        CoreTestSupport.SendBytesWithRetry(sb, "pong"u8, SendFlags.None, 2000);
        Assert.Equal("pong", CoreTestSupport.ReceiveStringWithTimeout(sc, 64, 2000));
    }

    [Fact]
    public void pair_tcp_connect_by_name_localhost()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var sb = new Socket(ctx, SocketType.Pair);
        using var sc = new Socket(ctx, SocketType.Pair);

        int port = CoreTestSupport.ExtractPort(CoreTestSupport.NewEndpoint("tcp",
            "pair-name"));
        string bindEndpoint = $"tcp://127.0.0.1:{port}";
        string connectEndpoint = $"tcp://localhost:{port}";

        sb.Bind(bindEndpoint);
        sc.Connect(connectEndpoint);
        Thread.Sleep(50);

        CoreTestSupport.SendBytesWithRetry(sc, Encoding.UTF8.GetBytes("hello"),
            SendFlags.None, 2000);
        Assert.Equal("hello", CoreTestSupport.ReceiveStringWithTimeout(sb, 64, 2000));
    }

    [Fact]
    public void poller_wait_span_api_reports_ready_count()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var sender = new Socket(ctx, SocketType.Pair);
        using var receiver = new Socket(ctx, SocketType.Pair);
        string endpoint = CoreTestSupport.NewEndpoint("inproc", "pair-poller-span");
        sender.Bind(endpoint);
        receiver.Connect(endpoint);
        Thread.Sleep(50);

        var poller = new Poller();
        poller.Add(receiver, PollEvents.PollIn);

        CoreTestSupport.SendBytesWithRetry(sender, "x"u8, SendFlags.None, 2000);

        PollEvent[] events = new PollEvent[4];
        int written = poller.Wait(events, 2000, out int totalReady);
        Assert.True(totalReady >= 1);
        Assert.True(written >= 1);
        Assert.NotNull(events[0].Socket);
        Assert.NotEqual(PollEvents.None, events[0].Revents & PollEvents.PollIn);
    }
}

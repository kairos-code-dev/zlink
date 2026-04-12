using System.Collections.Generic;
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
        using var sb = new PairSocket(ctx);
        using var sc = new PairSocket(ctx);

        string endpoint = CoreTestSupport.NewEndpoint(transport, "pair");
        sb.Bind(endpoint);
        sc.Connect(endpoint);
        Thread.Sleep(50);

        CoreTestSupport.SendWithRetry(sc, "ping"u8, 2000);
        Assert.Equal("ping", CoreTestSupport.ReceiveUtf8WithTimeout(sb, 2000));

        CoreTestSupport.SendWithRetry(sb, "pong"u8, 2000);
        Assert.Equal("pong", CoreTestSupport.ReceiveUtf8WithTimeout(sc, 2000));
    }

    [Theory]
    [InlineData("tcp")]
    [InlineData("ipc")]
    [InlineData("inproc")]
    public void pair_multipart_roundtrip(string transport)
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;
        if (!CoreTestSupport.IsTransportSupported(transport))
            return;

        using var ctx = new Context();
        using var server = new PairSocket(ctx);
        using var client = new PairSocket(ctx);

        string endpoint = CoreTestSupport.NewEndpoint(transport,
            "pair-multipart");
        server.Bind(endpoint);
        client.Connect(endpoint);
        Thread.Sleep(50);

        using Message part1 = Message.FromString("hello");
        using Message part2 = Message.FromString("world");
        client.Send(new[] { part1, part2 });

        Received received = server.Recv();
        try
        {
            Assert.Equal(2, received.Parts.Count);
            Assert.Equal("hello", received.Parts[0].GetString());
            Assert.Equal("world", received.Parts[1].GetString());
        }
        finally
        {
            foreach (Message part in received.Parts)
                part.Dispose();
        }
    }

    [Fact]
    public void pair_tcp_connect_by_name_localhost()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var sb = new PairSocket(ctx);
        using var sc = new PairSocket(ctx);

        int port = CoreTestSupport.ExtractPort(CoreTestSupport.NewEndpoint("tcp",
            "pair-name"));
        string bindEndpoint = $"tcp://127.0.0.1:{port}";
        string connectEndpoint = $"tcp://localhost:{port}";

        sb.Bind(bindEndpoint);
        sc.Connect(connectEndpoint);
        Thread.Sleep(50);

        CoreTestSupport.SendWithRetry(sc, "hello"u8, 2000);
        Assert.Equal("hello", CoreTestSupport.ReceiveUtf8WithTimeout(sb, 2000));
    }

    [Fact]
    public void poller_wait_span_api_reports_ready_count()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var sender = new PairSocket(ctx);
        using var receiver = new PairSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("inproc", "pair-poller-span");
        sender.Bind(endpoint);
        receiver.Connect(endpoint);
        Thread.Sleep(50);

        var poller = new Poller();
        poller.Add(receiver, PollEvents.PollIn);

        CoreTestSupport.SendWithRetry(sender, "x"u8, 2000);

        PollEvent[] events = new PollEvent[4];
        int written = poller.Wait(events, 2000, out int totalReady);
        Assert.True(totalReady >= 1);
        Assert.True(written >= 1);
        Assert.NotNull(events[0].Socket);
        Assert.NotEqual(PollEvents.None, events[0].Revents & PollEvents.PollIn);
    }

    [Fact]
    public void poller_modify_switches_event_mask()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var sender = new PairSocket(ctx);
        using var receiver = new PairSocket(ctx);
        using var poller = new Poller();
        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "pair-poller-modify");
        sender.Bind(endpoint);
        receiver.Connect(endpoint);
        Thread.Sleep(50);

        poller.Add(receiver, PollEvents.PollIn);
        Assert.Equal(1, poller.Count);

        CoreTestSupport.SendWithRetry(sender, "ping"u8, 2000);

        poller.Modify(receiver, PollEvents.PollOut);
        var events = new List<PollEvent>();
        Assert.Equal(1, poller.Wait(events, 1000));
        Assert.NotEmpty(events);
        Assert.NotEqual(PollEvents.None, events[0].Revents & PollEvents.PollOut);
        Assert.Equal(PollEvents.None, events[0].Revents & PollEvents.PollIn);

        poller.Modify(receiver, PollEvents.PollIn);
        events.Clear();
        Assert.Equal(1, poller.Wait(events, 1000));
        Assert.NotEmpty(events);
        Assert.NotEqual(PollEvents.None, events[0].Revents & PollEvents.PollIn);
        Assert.Equal(PollEvents.None, events[0].Revents & PollEvents.PollOut);

        Assert.Equal("ping", CoreTestSupport.ReceiveUtf8WithTimeout(receiver,
            2000));
    }

    [Fact]
    public void poller_fd_events_expose_registered_fd()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var sender = new PairSocket(ctx);
        using var receiver = new PairSocket(ctx);
        using var poller = new Poller();
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "pair-poller-fd");
        sender.Bind(endpoint);
        receiver.Connect(endpoint);
        Thread.Sleep(50);

        int fd = receiver.GetOption(SocketOptions.Fd);
        poller.AddFd(fd, PollEvents.PollIn);

        CoreTestSupport.SendWithRetry(sender, "fd"u8, 2000);

        var events = new List<PollEvent>();
        Assert.Equal(1, poller.Wait(events, 2000));
        Assert.NotEmpty(events);
        Assert.Equal(fd, events[0].Fd);
        Assert.Null(events[0].Socket);
        Assert.NotEqual(PollEvents.None, events[0].Revents & PollEvents.PollIn);

        Assert.Equal("fd", CoreTestSupport.ReceiveUtf8WithTimeout(receiver, 2000));
    }

    [Fact]
    public void receive_dontwait_throws_eagain_on_empty_queue()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var sender = new PairSocket(ctx);
        using var receiver = new PairSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "pair-try-recv-code");
        sender.Bind(endpoint);
        receiver.Connect(endpoint);
        Thread.Sleep(50);

        Assert.Throws<ZlinkRecvException>(() => receiver.Recv(RecvFlags.DontWait));
    }

    [Fact]
    public void pair_recv_handler_blocks_direct_receive_path()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var sender = new PairSocket(ctx);
        using var receiver = new PairSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "pair-recv-handler");
        sender.Bind(endpoint);
        receiver.Connect(endpoint);
        Thread.Sleep(50);

        receiver.OnReceive((routingId, parts) =>
        {
            foreach (Message part in parts)
                part.Dispose();
        });

        var ex = Assert.Throws<ZlinkRecvException>(() =>
            receiver.Recv(RecvFlags.DontWait));
        Assert.Equal(RecvResult.Busy, ex.Result);
    }

    [Fact]
    public void socket_monitor_attach_handler_snapshot_and_close_work()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var server = new PairSocket(ctx);
        using var client = new PairSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("tcp",
            "pair-monitor-shape");
        server.Bind(endpoint);

        using SocketMonitor monitor = server.MonitorOpen(SocketEvent.ConnectionReady
            | SocketEvent.Disconnected);
        int callbackCount = 0;
        monitor.OnEvent(_ => Interlocked.Increment(ref callbackCount));

        client.Connect(endpoint);
        Thread.Sleep(50);

        MonitorSnapshot snapshot = monitor.Snapshot();
        Assert.Equal(MonitorSourceKind.Socket, snapshot.SourceKind);
        Assert.True(snapshot.SendPendingMessages >= 0);

        Assert.True(CoreTestSupport.WaitUntil(() =>
            Volatile.Read(ref callbackCount) >= 1, 3000, 10));

        monitor.Close();
        Assert.Throws<ObjectDisposedException>(() => monitor.Snapshot());
    }

    [Fact]
    public void try_send_reports_backpressured_when_pair_hwm_is_exhausted()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var sender = new PairSocket(ctx);
        using var receiver = new PairSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "pair-try-send-backpressured");

        sender.SetOption(SocketOptions.SndHwm, 1);
        receiver.SetOption(SocketOptions.RcvHwm, 1);
        sender.Bind(endpoint);
        receiver.Connect(endpoint);
        Thread.Sleep(50);

        SendResult result = SendResult.Sent;
        for (int i = 0; i < 1024; i++)
        {
            using Message payload = Message.FromString($"bp-{i}");
            try
            {
                sender.Send(payload, SendFlags.DontWait);
                result = SendResult.Sent;
            }
            catch (ZlinkSubmitException ex)
            {
                result = ex.Result == SubmitResult.Backpressured
                    ? SendResult.Backpressured
                    : throw ex;
            }
            if (result != SendResult.Sent)
                break;
        }

        Assert.Equal(SendResult.Backpressured, result);
    }

    [Fact]
    public void try_send_reports_not_ready_for_unknown_router_peer()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var router = new RouterSocket(ctx);
        router.RouterOptions.Mandatory = true;

        using Message message = Message.FromString("no-route");
        var ex = Assert.Throws<ZlinkSubmitException>(() =>
            router.Send("UNKNOWN", message, SendFlags.DontWait));
        Assert.Equal(SubmitResult.NotConnected, ex.Result);
    }
}

using System.Collections.Generic;
using System.Threading;
using Xunit;

namespace Systems.Zlink.Tests;

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
        client.Send().Message(part1).Message(part2).Submit();

        var received = new Received();
        server.Recv(received);
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
        poller.Add(receiver, PollEventFlags.PollIn);

        CoreTestSupport.SendWithRetry(sender, "x"u8, 2000);

        var events = new PollEvent[4];
        int written = poller.Wait(events, TimeSpan.FromMilliseconds(2000), out _);
        Assert.True(written > 0);
        Assert.NotNull(events[0].Socket);
        Assert.NotEqual(PollEventFlags.None, events[0].Revents & PollEventFlags.PollIn);
    }

    [Fact]
    public void poller_clear_removes_registered_items()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var sender = new PairSocket(ctx);
        using var receiver = new PairSocket(ctx);
        using var poller = new Poller();
        string endpoint = CoreTestSupport.NewEndpoint("inproc", "pair-poller-clear");
        sender.Bind(endpoint);
        receiver.Connect(endpoint);
        Thread.Sleep(50);

        poller.Add(receiver, PollEventFlags.PollIn);
        Assert.Equal(1, poller.Size);

        poller.Clear();

        Assert.Equal(0, poller.Size);
        var clearEvents = new PollEvent[1];
        Assert.Equal(0, poller.Wait(clearEvents, TimeSpan.Zero, out _));
    }

    [Fact]
    public void poller_modify_switches_socket_event_mask()
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

        poller.Add(receiver, PollEventFlags.PollIn);
        Assert.Equal(1, poller.Size);

        CoreTestSupport.SendWithRetry(sender, "ping"u8, 2000);

        var events = new PollEvent[1];
        int written = poller.Wait(events, TimeSpan.FromMilliseconds(2000),
            out _);
        Assert.Equal(1, written);
        Assert.NotEqual(PollEventFlags.None,
            events[0].Revents & PollEventFlags.PollIn);

        Assert.Equal("ping", CoreTestSupport.ReceiveUtf8WithTimeout(receiver,
            2000));

        poller.Modify(receiver, PollEventFlags.PollOut);

        CoreTestSupport.SendWithRetry(sender, "pong"u8, 2000);

        // POLLOUT is send-recovery readiness, not generic transport writability.
        // After switching away from POLLIN, queued receive data must not surface.
        Array.Clear(events);
        written = poller.Wait(events, TimeSpan.FromMilliseconds(100), out _);
        Assert.Equal(0, written);

        poller.Modify(receiver, PollEventFlags.PollIn);
        written = poller.Wait(events, TimeSpan.FromMilliseconds(2000), out _);
        Assert.Equal(1, written);
        Assert.NotEqual(PollEventFlags.None,
            events[0].Revents & PollEventFlags.PollIn);
        Assert.Equal(PollEventFlags.None,
            events[0].Revents & PollEventFlags.PollOut);
        Assert.Equal("pong", CoreTestSupport.ReceiveUtf8WithTimeout(receiver,
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
        poller.AddFd(fd, PollEventFlags.PollIn);

        CoreTestSupport.SendWithRetry(sender, "fd"u8, 2000);

        var events = new PollEvent[1];
        int written = poller.Wait(events, TimeSpan.FromMilliseconds(2000), out _);
        Assert.Equal(1, written);
        Assert.Equal(fd, events[0].Fd);
        Assert.Null(events[0].Socket);
        Assert.NotEqual(PollEventFlags.None, events[0].Revents & PollEventFlags.PollIn);

        Assert.Equal("fd", CoreTestSupport.ReceiveUtf8WithTimeout(receiver, 2000));
    }

    [Fact]
    public void receive_dontwait_returns_null_on_empty_queue()
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

        var probe = new Received();
        Assert.False(receiver.Recv(probe, RecvFlags.DontWait));
    }

    [Fact]
    public void pair_direct_receive_path_receives_messages()
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

        CoreTestSupport.SendWithRetry(sender, "pair-direct"u8, 2000);
        Assert.Equal("pair-direct", CoreTestSupport.ReceiveUtf8WithTimeout(
            receiver, 2000));
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

        using ISocketMonitor monitor = server.MonitorOpen(SocketEvent.ConnectionReady
            | SocketEvent.Disconnected);
        int callbackCount = 0;
        monitor.OnEvent(_ => Interlocked.Increment(ref callbackCount));

        client.Connect(endpoint);
        Thread.Sleep(50);

        MonitorSnapshot snapshot = monitor.Snapshot();
        Assert.Equal<MonitorSourceKind>(MonitorSourceKind.Socket, snapshot.SourceKind);
        Assert.True(snapshot.SndPendingMsgs >= 0);

        Assert.True(CoreTestSupport.WaitUntil(() =>
            Volatile.Read(ref callbackCount) >= 1, 15000, 10));

        monitor.Close();
        Assert.Throws<ObjectDisposedException>(() => monitor.Snapshot());
    }

    [Fact]
    public void send_nonblocking_reports_backpressured_when_pair_hwm_is_exhausted()
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
        byte[] payloadBytes = new byte[64 * 1024];
        for (int i = 0; i < 16 * 1024; i++)
        {
            using Message payload = Message.FromBytes(payloadBytes);
            result = sender.Send().Message(payload).Flags(SendFlags.DontWait)
                .Submit()
                ? SendResult.Sent
                : SendResult.Backpressured;
            if (result != SendResult.Sent)
                break;
        }

        Assert.Equal(SendResult.Backpressured, result);
    }

    [Fact]
    public void send_nonblocking_returns_false_only_for_backpressured_pair_queue()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var sender = new PairSocket(ctx);
        using var receiver = new PairSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "pair-public-try-send-backpressured");

        sender.SetOption(SocketOptions.SndHwm, 1);
        receiver.SetOption(SocketOptions.RcvHwm, 1);
        sender.Bind(endpoint);
        receiver.Connect(endpoint);
        Thread.Sleep(50);

        bool sent = true;
        byte[] payloadBytes = new byte[64 * 1024];
        for (int i = 0; i < 16 * 1024; i++)
        {
            using Message payload = Message.FromBytes(payloadBytes);
            sent = sender.Send().Message(payload).Flags(SendFlags.DontWait)
                .Submit();
            if (!sent)
                break;
        }

        Assert.False(sent);
    }

    [Fact]
    public void send_nonblocking_reports_not_ready_for_unknown_router_peer()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var router = new RouterSocket(ctx);
        router.Options.Mandatory = true;

        using Message message = Message.FromString("no-route");
        var ex = Assert.Throws<ZlinkSubmitException>(() =>
            router.Send(RoutingId.FromBytes("UNKNOWN"u8)).Message(message)
                .Flags(SendFlags.DontWait).Submit());
        Assert.Equal(ZlinkSubmitException.ErrorCode.NotConnected, ex.Result);
    }

    [Fact]
    public void send_nonblocking_throws_not_ready_for_unknown_router_peer()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var router = new RouterSocket(ctx);
        router.Options.Mandatory = true;

        using Message message = Message.FromString("no-route");
        var ex = Assert.Throws<ZlinkSubmitException>(() =>
            router.Send(RoutingId.FromBytes("UNKNOWN"u8)).Message(message)
                .Flags(SendFlags.DontWait).Submit());
        Assert.Equal(ZlinkSubmitException.ErrorCode.NotConnected, ex.Result);
    }
}

using System;
using System.Threading;
using Xunit;

namespace Zlink.Tests;

public sealed class test_monitor_contract
{
    [Fact]
    public void socket_monitor_receive_reports_connection_ready_event()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var server = new PairSocket(ctx);
        using var client = new PairSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "monitor-recv");
        server.Bind(endpoint);

        using SocketMonitor monitor = server.MonitorOpen();
        client.Connect(endpoint);

        SocketMonitorEvent evt = monitor.Recv();
        Assert.Equal(MonitorEventType.Accepted, evt.Event);
    }

    [Fact]
    public void socket_monitor_attach_handler_snapshot_and_close_contract()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var server = new PairSocket(ctx);
        using var client = new PairSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "monitor-handler");
        server.Bind(endpoint);

        using var events = new CallbackEventQueue<SocketMonitorEvent>();
        using SocketMonitor monitor = server.MonitorOpen(
            SocketEvent.ConnectionReady | SocketEvent.Disconnected);
        monitor.OnEvent(events.Enqueue);

        client.Connect(endpoint);

        Assert.True(events.TryDequeue(20000, out SocketMonitorEvent evt));
        Assert.Equal(MonitorEventType.ConnectionReady, evt.Event);

        MonitorSnapshot snapshot = monitor.Snapshot();
        Assert.Equal<SourceKind>(SourceKind.Socket, snapshot.SourceKind);
        Assert.True(snapshot.SndPendingMsgs >= 0);
        Assert.True(snapshot.AutoHwmProfile >= 0);
        Assert.True(snapshot.AutoHwmPolicyClass >= 0);
        Assert.True(snapshot.AutoHwmUnitBudgetBytes >= 0);
        Assert.True(snapshot.AutoHwmSizeCap >= 0);
        Assert.True(snapshot.AutoHwmSocketMessageSlots >= 0);

        monitor.Close();
        Assert.Throws<ObjectDisposedException>(() => monitor.Snapshot());
    }

    [Fact]
    public void socket_monitor_try_receive_returns_null_when_queue_empty()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var server = new PairSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("tcp",
            "monitor-try-recv-empty");
        server.Bind(endpoint);

        using SocketMonitor monitor = server.MonitorOpen();

        Assert.Null(monitor.Recv(true));
    }

    [Fact]
    public void socket_monitor_ignore_handler_switches_to_callback_only_model()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var server = new PairSocket(ctx);
        using var client = new PairSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "monitor-ignore");
        server.Bind(endpoint);

        using SocketMonitor monitor = server.MonitorOpen(SocketEvent.ConnectionReady);
        monitor.OnEvent(SocketMonitor.IgnoreHandler);

        client.Connect(endpoint);

        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            try
            {
                _ = monitor.Snapshot();
                return true;
            }
            catch (ZlinkConfigException)
            {
                return false;
            }
        }, 3000));

        ZlinkRecvException error = Assert.Throws<ZlinkRecvException>(() => monitor.Recv());
        Assert.Equal(RecvResult.Busy, error.Result);
    }
}

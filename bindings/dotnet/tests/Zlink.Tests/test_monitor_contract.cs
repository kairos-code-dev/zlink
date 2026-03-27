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

        using SocketMonitor monitor = server.OpenMonitor(
            SocketEvent.ConnectionReady);
        client.Connect(endpoint);

        SocketMonitorEvent evt = monitor.Receive();
        Assert.Equal(SocketEvent.ConnectionReady, evt.Event);
        Assert.False(string.IsNullOrEmpty(evt.RoutingId));
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
        using SocketMonitor monitor = server.OpenMonitor(
            SocketEvent.ConnectionReady | SocketEvent.Disconnected);
        monitor.AttachHandler(events.Enqueue);

        client.Connect(endpoint);

        Assert.True(events.TryDequeue(3000, out SocketMonitorEvent evt));
        Assert.Equal(SocketEvent.ConnectionReady, evt.Event);

        MonitorSnapshot snapshot = monitor.Snapshot();
        Assert.Equal(MonitorSourceKind.Socket, snapshot.SourceKind);
        Assert.True(snapshot.ReadyCount >= 0);

        monitor.Close();
        Assert.Throws<ObjectDisposedException>(() => monitor.Snapshot());
    }
}

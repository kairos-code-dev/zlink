using System;
using Xunit;
using Zlink.Service;

namespace Zlink.Tests;

public sealed class test_service_monitor_contract
{
    [Fact]
    public void discovery_open_monitor_close_contract()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var discovery = new Discovery(ctx, ServiceType.Spot,
            "svc-monitor");
        using ServiceMonitor monitor = discovery.MonitorOpen();

        monitor.Close();

        Assert.Throws<ObjectDisposedException>(() => monitor.Recv());
    }

    [Fact]
    public void spot_open_monitor_close_contract()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = new Spot(node);
        using ServiceMonitor monitor = spot.MonitorOpen();

        monitor.Close();

        Assert.Throws<ObjectDisposedException>(() => monitor.Recv());
    }

    [Fact]
    public void spot_monitor_try_receive_returns_null_when_queue_empty()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = new Spot(node);
        using ServiceMonitor monitor = spot.MonitorOpen();

        Assert.False(monitor.TryRecv(out _));
    }

    [Fact]
    public void spot_monitor_receive_blocks_until_subscription_events_and_snapshot_reports_send_ready()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;
        if (!CoreTestSupport.IsTransportSupported("tcp"))
            return;

        using var ctx = new Context();
        using var pubNode = new SpotNode(ctx);
        using var subNode = new SpotNode(ctx);
        using var publisher = new Spot(pubNode);
        using var subscriber = new Spot(subNode);
        const string topic = "room:lobby";
        string endpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-service-monitor");

        pubNode.Bind(endpoint);
        subNode.ConnectPeer(endpoint);

        using ServiceMonitor pubMonitor =
            publisher.MonitorOpen(
                ServiceMonitorEvents.SpotFirstDeliveryReadyChanged);
        using ServiceMonitor subMonitor =
            subscriber.MonitorOpen(
                ServiceMonitorEvents.SpotFilterApplied
                | ServiceMonitorEvents.SpotSubscriptionReadyChanged);

        subscriber.SetSubscription(topic);

        ServiceMonitorEvent filterEvent = subMonitor.Recv();
        Assert.NotEqual(ServiceMonitorEvents.None,
            filterEvent.EventType & ServiceMonitorEvents.SpotFilterApplied);
        Assert.Equal(topic, filterEvent.Subject);

        ServiceMonitorEvent readyEvent = subMonitor.Recv();
        Assert.NotEqual(ServiceMonitorEvents.None,
            readyEvent.EventType
                & ServiceMonitorEvents.SpotSubscriptionReadyChanged);
        Assert.Equal(endpoint, readyEvent.Endpoint);

        MonitorSnapshot snapshot = pubMonitor.Snapshot();
        Assert.NotEqual(MonitorState.None,
            snapshot.StateFlags & MonitorState.SendReady);
    }
}

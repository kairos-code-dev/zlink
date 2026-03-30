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
        using ServiceMonitor monitor = discovery.OpenMonitor();

        monitor.Close();

        Assert.Throws<ObjectDisposedException>(() => monitor.Receive());
    }

    [Fact]
    public void spot_open_monitor_close_contract()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var spot = new Spot(ctx);
        using ServiceMonitor monitor = spot.OpenMonitor();

        monitor.Close();

        Assert.Throws<ObjectDisposedException>(() => monitor.Receive());
    }

    [Fact]
    public void spot_monitor_try_receive_returns_null_when_queue_empty()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var spot = new Spot(ctx);
        using ServiceMonitor monitor = spot.OpenMonitor();

        Assert.Null(monitor.TryReceive());
    }
}

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
}

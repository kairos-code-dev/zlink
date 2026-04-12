using Xunit;
using Zlink.Service;

namespace Zlink.Tests;

public sealed class test_attach_discovery_contract
{
    [Fact]
    public void attach_discovery_blocks_manual_connect_and_unbind()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var dealer = new DealerSocket(ctx);
        using var discovery = new Discovery(ctx, ServiceType.Socket,
            "attach-discovery");

        dealer.AttachDiscovery(discovery);

        Assert.Throws<ZlinkConnectException>(() =>
            dealer.Connect("tcp://127.0.0.1:5555"));
        Assert.Throws<ZlinkConnectException>(() =>
            dealer.Disconnect("tcp://127.0.0.1:5555"));
        Assert.Throws<ZlinkConnectException>(() =>
            dealer.Unbind("tcp://127.0.0.1:5555"));
    }
}

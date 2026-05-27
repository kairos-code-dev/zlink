using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_attach_discovery_contract
{
    [Fact]
    public void attach_discovery_blocks_manual_connect_and_unbind()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var discovery = ctx.CreateDiscovery(AutoConnectType.ClientServer, "attach-discovery");
        var dealer = ctx.CreateDealerSocket();

        dealer.AttachDiscovery(discovery);

        Assert.Throws<ZlinkConnectException>(() =>
            dealer.Connect("tcp://127.0.0.1:5555"));
        Assert.Throws<ZlinkConnectException>(() =>
            dealer.Disconnect("tcp://127.0.0.1:5555"));
        Assert.Throws<ZlinkConnectException>(() =>
            dealer.Unbind("tcp://127.0.0.1:5555"));

        Assert.Throws<ZlinkCloseException>(() => dealer.Close());
    }

    [Fact]
    public void dispose_releases_attached_socket_wrapper_without_public_close()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var discovery = ctx.CreateDiscovery(AutoConnectType.ClientServer, "attach-discovery-dispose");
        var dealer = ctx.CreateDealerSocket();

        dealer.AttachDiscovery(discovery);
        dealer.Dispose();
    }

    [Fact]
    public void discovery_destroy_then_socket_dispose_does_not_double_close_attached_socket()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        var discovery = ctx.CreateDiscovery(AutoConnectType.ClientServer, "attach-discovery-double-close");
        var dealer = ctx.CreateDealerSocket();

        dealer.AttachDiscovery(discovery);
        discovery.Dispose();
        dealer.Dispose();
    }
}

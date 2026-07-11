namespace Zlink.Framework.UnitTests;

public sealed class ChannelRuntimeOptionsTests
{
    [Fact]
    public void ClientServerRuntimeOptionsExposeEveryFrozenAspect()
    {
        var serverSocketRecipe = new ZLinkSocketConfig
        {
            MaxMessageSize = 1024,
            Weight = 73
        };
        var clientSocketRecipe = new ZLinkSocketConfig
        {
            SendHighWaterMark = 17,
            Weight = 42
        };
        var serverRoutingRecipe = new ZLinkRouteConfig
        {
            RequireKnownPeer = true,
            AllowPeerHandover = true,
            EnablePeerProbe = true,
            ConnectRoutingId = RoutingId.From("peer")
        };
        var clientRoutingRecipe = new ZLinkOutboundRouteConfig
        {
            ProbeRouterOnConnect = true
        };
        var runtimeOptions = new ZLinkClientServerRuntimeOptions(
            new ZLinkFrozenSocketConfig(serverSocketRecipe),
            new ZLinkFrozenRouteConfig(serverRoutingRecipe),
            new ZLinkFrozenSocketConfig(clientSocketRecipe),
            new ZLinkFrozenOutboundRouteConfig(clientRoutingRecipe));

        var serverSocket = runtimeOptions.ConfigureServerSocket();
        var clientSocket = runtimeOptions.ConfigureClientSocket();
        var serverRouting = runtimeOptions.ConfigureServerRouting();
        var clientRouting = runtimeOptions.ConfigureClientRouting();

        Assert.Equal(1024, serverSocket.MaxMessageSize);
        Assert.Equal(73, serverSocket.Weight);
        Assert.Equal(17, clientSocket.SendHighWaterMark);
        Assert.Equal(42, clientSocket.Weight);
        Assert.True(serverRouting.RequireKnownPeer);
        Assert.True(serverRouting.AllowPeerHandover);
        Assert.True(serverRouting.EnablePeerProbe);
        Assert.Equal(RoutingId.From("peer"), serverRouting.ConnectRoutingId);
        Assert.True(clientRouting.ProbeRouterOnConnect);

        Assert.Throws<ZLinkConfigurationException>(() => serverSocket.MaxMessageSize = 2048);
        Assert.Throws<ZLinkConfigurationException>(() => clientSocket.Weight = 0);
        Assert.Throws<ZLinkConfigurationException>(() => serverRouting.RequireKnownPeer = false);
        Assert.Throws<ZLinkConfigurationException>(() => clientRouting.ProbeRouterOnConnect = false);
    }

    [Fact]
    public void ClientServerRuntimeOptionsRejectOnlyRolesMissingFromTheNode()
    {
        var runtimeOptions = new ZLinkClientServerRuntimeOptions(
            null,
            null,
            new ZLinkFrozenSocketConfig(new ZLinkSocketConfig()),
            new ZLinkFrozenOutboundRouteConfig(new ZLinkOutboundRouteConfig()));

        _ = runtimeOptions.ConfigureClientSocket();
        _ = runtimeOptions.ConfigureClientRouting();
        Assert.Throws<ZLinkConfigurationException>(() => runtimeOptions.ConfigureServerSocket());
        Assert.Throws<ZLinkConfigurationException>(() => runtimeOptions.ConfigureServerRouting());
    }
}

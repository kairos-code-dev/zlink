using Zlink;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.UnitTests;

public sealed class SpotPeerConnectionSetTests
{
    [Fact]
    public void RouterDiscoveredAllowsRidAssociationForExistingEndpoint()
    {
        var connections = new ZLinkSpotPeerConnectionSet();
        var endpoint = "inproc://peer-router";

        Assert.True(connections.TryAddRouterDiscovered(endpoint));
        Assert.True(connections.TryAddRouterDiscovered(endpoint, RoutingId.From("peer-node")));
        Assert.False(connections.TryAddRouterDiscovered(endpoint, RoutingId.From("peer-node")));
    }

    [Fact]
    public void RouterManualSuppressesDiscoveredRidAssociation()
    {
        var connections = new ZLinkSpotPeerConnectionSet();
        var endpoint = "inproc://peer-router";

        Assert.True(connections.TryAddRouterManual(endpoint));
        Assert.False(connections.TryAddRouterDiscovered(endpoint, RoutingId.From("peer-node")));
    }
}

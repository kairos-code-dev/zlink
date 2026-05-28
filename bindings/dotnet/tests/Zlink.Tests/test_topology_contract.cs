using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_topology_contract
{
    [Fact]
    public void registry_empty_queries_return_empty_arrays()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var registry = ctx.CreateRegistry();

        RegistryStatus snapshot = registry.Status();
        Assert.True(snapshot.TopologyEntryCount >= 0);
        Assert.Empty(registry.ServiceSummary());
        Assert.Empty(registry.Topology());
        Assert.Empty(registry.Topology());
        Assert.Empty(registry.MemberPeers("missing"));
    }

    [Fact]
    public void spot_node_empty_queries_return_empty_arrays()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();

        SpotNodeStatus snapshot = node.Status();
        Assert.True(snapshot.SubjectCount >= 0);
        Assert.Empty(node.Peers());
        Assert.Empty(node.Subjects());
    }

    [Fact]
    public void spot_and_spot_node_expose_routing_identity_surface()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var spot = node.CreateSpot();

        RoutingId nodeRid = CoreTestSupport.RoutingIdUtf8("spot-node-id");
        RoutingId spotRid = CoreTestSupport.RoutingIdUtf8("spot-id");

        node.SetRoutingId(nodeRid);
        spot.SetRoutingId(spotRid);

        Assert.Equal(nodeRid, node.RoutingId);
        Assert.Equal(spotRid, spot.RoutingId);
    }
}

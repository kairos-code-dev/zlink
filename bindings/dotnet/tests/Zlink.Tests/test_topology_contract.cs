using Xunit;

namespace Zlink.Tests;

public sealed class test_topology_contract
{
    [Fact]
    public void registry_empty_queries_return_empty_arrays()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);

        RegistryStatus snapshot = registry.StatusSnapshot();
        Assert.True(snapshot.TopologyEntryCount >= 0);
        Assert.Empty(registry.ServiceSummarySnapshot());
        Assert.Empty(registry.TopologySnapshot());
        Assert.Empty(registry.TopologyQuery());
        Assert.Empty(registry.MemberPeers("missing"));
    }

    [Fact]
    public void spot_node_empty_queries_return_empty_arrays()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);

        SpotNodeStatus snapshot = node.StatusSnapshot();
        Assert.True(snapshot.SubjectCount >= 0);
        Assert.Empty(node.PeersSnapshot());
        Assert.Empty(node.SubjectsSnapshot());
    }

    [Fact]
    public void spot_and_spot_node_expose_routing_identity_surface()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = node.CreateSpot();

        RoutingId nodeRid = CoreTestSupport.RoutingIdUtf8("spot-node-id");
        RoutingId spotRid = CoreTestSupport.RoutingIdUtf8("spot-id");

        node.SetRoutingId(nodeRid);
        spot.SetRoutingId(spotRid);

        Assert.Equal(nodeRid, node.RoutingId);
        Assert.Equal(spotRid, spot.RoutingId);
    }
}

using Xunit;
using Zlink.Service;

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

        RegistryStatus snapshot = registry.Snapshot();
        Assert.True(snapshot.TopologyEntryCount >= 0);
        Assert.Empty(registry.ServiceSummary());
        Assert.Empty(registry.TopologySnapshot());
        Assert.Empty(registry.TopologyQuery());
        Assert.Empty(registry.MemberPeers(ServiceType.Spot, "missing"));
    }

    [Fact]
    public void spot_node_empty_queries_return_empty_arrays()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);

        SpotNodeStatus snapshot = node.Snapshot();
        Assert.True(snapshot.SubjectCount >= 0);
        Assert.Empty(node.Peers());
        Assert.Empty(node.Subjects());
    }
}

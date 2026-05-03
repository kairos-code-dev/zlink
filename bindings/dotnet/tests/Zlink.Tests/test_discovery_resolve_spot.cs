using System;
using System.Linq;
using Xunit;

namespace Zlink.Tests;

public sealed class test_discovery_resolve_spot
{
    [Fact]
    public void discovery_resolve_spot_returns_node_rid_and_clears_after_spot_destroy()
    {
        if (!CoreTestSupport.IsNativeAvailable()
            || !CoreTestSupport.IsTransportSupported("tcp"))
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        using var discovery = new Discovery(ctx, AutoConnectType.SpotMesh,
            "dotnet-resolve-spot");
        using var node = new SpotNode(ctx);
        Spot? spot = node.CreateSpot();

        try
        {
            RoutingId nodeRid = CoreTestSupport.RoutingIdUtf8("dotnet-node-a");
            RoutingId spotRid = CoreTestSupport.RoutingIdUtf8("dotnet-spot-a");
            string registryPub = CoreTestSupport.NewEndpoint("tcp",
                "dotnet-resolve-reg-pub");
            string registryRouter = CoreTestSupport.NewEndpoint("tcp",
                "dotnet-resolve-reg-router");
            string nodeEndpoint = CoreTestSupport.NewEndpoint("tcp",
                "dotnet-resolve-node");

            registry.Bind(registryPub, registryRouter);
            registry.SetBroadcastInterval(50);
            Assert.False(discovery.SpotOwnerSyncEnabled);
            discovery.SpotOwnerSyncEnabled = true;
            discovery.ConnectRegistry(registryRouter);

            node.SetRoutingId(nodeRid);
            spot.SetRoutingId(spotRid);
            node.Bind(nodeEndpoint);
            node.AttachDiscovery(discovery);

            Assert.True(CoreTestSupport.WaitUntil(
                () => RegistryHasNodeMember(registry, nodeEndpoint, nodeRid)
                    && RegistryHasSpotTopology(registry, nodeEndpoint, spotRid)
                    && TryResolveSpot(discovery, spotRid, out RoutingId resolved)
                    && resolved == nodeRid,
                timeoutMs: 10000,
                sleepMs: 25));

            spot.Dispose();
            spot = null;

            Assert.True(CoreTestSupport.WaitUntil(
                () => ResolveSpotReturnsNoEntity(discovery, spotRid),
                timeoutMs: 10000,
                sleepMs: 25));
        }
        finally
        {
            spot?.Dispose();
        }
    }

    private static bool RegistryHasNodeMember(Registry registry,
        string nodeEndpoint, RoutingId nodeRid)
    {
        return registry.MemberPeers("dotnet-resolve-spot").Any(entry =>
            entry.ServiceRole == ServiceRole.Spot
            && entry.Endpoint == nodeEndpoint
            && entry.RoutingId == nodeRid);
    }

    private static bool RegistryHasSpotTopology(Registry registry,
        string nodeEndpoint, RoutingId spotRid)
    {
        RegistryTopologyEntry[] entries = registry.TopologyQuery(
            new RegistryTopologyFilter(
                AutoConnectType: AutoConnectType.SpotMesh,
                ServiceKind: ServiceKind.SpotPub,
                ServiceRole: ServiceRole.Spot,
                ChannelName: "dotnet-resolve-spot",
                RoutingId: spotRid));

        return entries.Any(entry =>
            entry.Endpoint == nodeEndpoint
            && entry.RoutingId == spotRid
            && entry.State == TopologyState.Ready);
    }

    private static bool TryResolveSpot(Discovery discovery, RoutingId spotRid,
        out RoutingId resolved)
    {
        try
        {
            resolved = discovery.ResolveSpot(spotRid);
            return true;
        }
        catch (ZlinkConfigException)
        {
            resolved = default;
            return false;
        }
    }

    private static bool ResolveSpotReturnsNoEntity(Discovery discovery,
        RoutingId spotRid)
    {
        try
        {
            _ = discovery.ResolveSpot(spotRid);
            return false;
        }
        catch (ZlinkConfigException ex)
        {
            return ex.InternalErrno == 2;
        }
    }
}

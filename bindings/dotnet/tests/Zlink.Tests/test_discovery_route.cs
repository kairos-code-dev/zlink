using System;
using System.Linq;
using System.Text;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_discovery_route
{
    [Fact]
    public void discovery_route_api_resolves_owner_bound_route_and_cleans_up_owner()
    {
        if (!CoreTestSupport.IsNativeAvailable()
            || !CoreTestSupport.IsTransportSupported("tcp"))
            return;

        const string channel = "dotnet-route-api";
        using var registryContext = Zlink.CreateContext();
        using var ownerContext = Zlink.CreateContext();
        using var resolverContext = Zlink.CreateContext();
        using var registry = registryContext.CreateRegistry();
        using var ownerDiscovery = ownerContext.CreateDiscovery(AutoConnectType.ClientServer, channel);
        using var resolverDiscovery = resolverContext.CreateDiscovery(AutoConnectType.ClientServer, channel);
        var owner = ownerContext.CreateRouterSocket();

        string registryPub = CoreTestSupport.NewEndpoint("tcp",
            "dotnet-route-reg-pub");
        string registryRouter = CoreTestSupport.NewEndpoint("tcp",
            "dotnet-route-reg-router");
        string ownerEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "dotnet-route-owner");
        RoutingId ownerRid = CoreTestSupport.RoutingIdUtf8("dotnet-route-owner");
        byte[] routeKey = Encoding.UTF8.GetBytes("actor-a");
        byte[] routeValue = Encoding.UTF8.GetBytes("session-route-v1");

        registry.Bind(registryPub, registryRouter);
        registry.SetBroadcastInterval(TimeSpan.FromMilliseconds(50));
        ownerDiscovery.ConnectRegistry(registryRouter);
        resolverDiscovery.ConnectRegistry(registryRouter);

        owner.SetRoutingId(ownerRid);
        owner.AttachDiscovery(ownerDiscovery);
        owner.Bind(ownerEndpoint);

        Assert.True(CoreTestSupport.WaitUntil(
            () => registry.MemberPeers(channel).Any(entry =>
                entry.ServiceRole == ServiceRole.Router
                && entry.RoutingId == ownerRid),
            timeoutMs: 10000,
            sleepMs: 25));

        Assert.True(CoreTestSupport.WaitUntil(
            () => TryBindRoute(ownerDiscovery, routeKey, routeValue),
            timeoutMs: 10000,
            sleepMs: 25));

        Assert.True(CoreTestSupport.WaitUntil(
            () => TryResolveRoute(resolverDiscovery, routeKey, ownerRid,
                routeValue),
            timeoutMs: 10000,
            sleepMs: 25));

        ownerDiscovery.Dispose();

        Assert.True(CoreTestSupport.WaitUntil(
            () => ResolveRouteReturnsNoEntity(resolverDiscovery, routeKey),
            timeoutMs: 10000,
            sleepMs: 25));
    }

    private static bool TryBindRoute(IDiscovery discovery, byte[] key,
        byte[] value)
    {
        try
        {
            discovery.BindRoute(DiscoveryRouteKind.ActorSession, key, value);
            return true;
        }
        catch (ZlinkConfigException)
        {
            return false;
        }
    }

    private static bool TryResolveRoute(IDiscovery discovery, byte[] key,
        RoutingId expectedOwner, byte[] expectedValue)
    {
        try
        {
            using DiscoveryRoute route =
                discovery.ResolveRoute(DiscoveryRouteKind.ActorSession, key);
            return route.OwnerRoutingId == expectedOwner
                && route.Value.AsReadOnlySpan().SequenceEqual(expectedValue);
        }
        catch (ZlinkConfigException)
        {
            return false;
        }
    }

    private static bool ResolveRouteReturnsNoEntity(IDiscovery discovery,
        byte[] key)
    {
        try
        {
            using DiscoveryRoute route =
                discovery.ResolveRoute(DiscoveryRouteKind.ActorSession, key);
            return false;
        }
        catch (ZlinkConfigException ex)
        {
            return ex.InternalErrno == 2;
        }
    }
}

using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.Tests;

public sealed class DiscoveryIntegrationTests
{
    [Fact]
    public async Task RegistryActorRoutes_Publishes_FrameworkActorRoute()
    {
        var registryPubEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var registryRouterEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var routeEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var spotNodeEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var spotChannel = $"registry.actor.routes.{Guid.NewGuid():N}";
        var routeRid = RoutingId.FromString("1101");

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var frameworkBuilder = Host.CreateApplicationBuilder();
        frameworkBuilder.Services.AddSingleton<StreamIntegrationTests.ActorDispatchRecorder>();
        frameworkBuilder.Services.AddScoped<StreamIntegrationTests.GatewayActorFactory>();
        frameworkBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add(registryRouterEndpoint));
            options.UseSpotDiscovery(spotChannel, discovery => discovery.Add(registryRouterEndpoint));
            options.UseRegistryActorRoutes("registry-actor-sync");
            options.AddActorFactory<StreamIntegrationTests.GatewayActorFactory>("player");
            options.AddRouteMeshChannel("play", route =>
            {
                route.Bind(routeEndpoint);
                route.ConfigureRouting(routing => routing.RoutingId = routeRid);
            });
            options.AddSpotNode("actor-sync-node", spot =>
            {
                spot.Bind(spotNodeEndpoint);
                spot.EnableRouter(router => router.ConfigureRouting(
                    routing => routing.RoutingId = routeRid));
            });
        });

        using var registryHost = registryBuilder.Build();
        using var frameworkHost = frameworkBuilder.Build();
        await registryHost.StartAsync();
        await frameworkHost.StartAsync();

        var runtime = frameworkHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var state = await runtime.GetStartedStateForRoutingAsync(CancellationToken.None);
        var discovery = state.RouteChannels["play"].Discovery;
        Assert.NotNull(discovery);
        Assert.False(discovery.ActorRouteSyncEnabled);
        var spotDiscovery = Assert.Single(state.SpotDiscoveries.Values);
        Assert.True(spotDiscovery.ActorRouteSyncEnabled);

        var manager = frameworkHost.Services.GetRequiredService<IZLinkActorManager>();
        _ = await manager.GetOrCreateAsync("actor-sync-player", "player");
        var resolver = frameworkHost.Services.GetRequiredService<IZLinkActorPlayRouteResolver>();
        var route = await RetryAsync(
            () => resolver.ResolvePlayRouteAsync("actor-sync-player", CancellationToken.None).AsTask(),
            result => result.TargetNodeRid == routeRid
                && result.ActorId == "actor-sync-player"
                && result.CurrentSpotRid.Size > 0
                && result.CurrentSpotKind == ZLinkSpotKind.Entry,
            TimeSpan.FromSeconds(5));

        Assert.Equal("play", route.RouterChannelId);
        Assert.Equal("actor-sync-player", route.ActorId);
        Assert.Equal(routeRid, route.TargetNodeRid);
        Assert.True(route.CurrentSpotRid.Size > 0);
        Assert.Equal(ZLinkSpotKind.Entry, route.CurrentSpotKind);

        await frameworkHost.StopAsync();
        await registryHost.StopAsync();
    }

    [Fact]
    public async Task RegistrySpotRoutes_Enables_SpotOwnerSync()
    {
        var registryPubEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var registryRouterEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var routeEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var spotNodeEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var spotChannel = $"registry.spot.routes.{Guid.NewGuid():N}";

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var frameworkBuilder = Host.CreateApplicationBuilder();
        frameworkBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add(registryRouterEndpoint));
            options.UseSpotDiscovery(spotChannel, discovery => discovery.Add(registryRouterEndpoint));
            options.UseRegistrySpotRoutes("registry-spot-sync");
            options.AddRouteMeshChannel("play", route => route.Bind(routeEndpoint));
            options.AddSpotNode("spot-sync-node", spot =>
            {
                spot.Bind(spotNodeEndpoint);
                spot.AddSpotFactory<SpotIntegrationTests.LocalSubscriberStageSpot>("registry-stage");
            });
        });

        using var registryHost = registryBuilder.Build();
        using var frameworkHost = frameworkBuilder.Build();
        await registryHost.StartAsync();
        await frameworkHost.StartAsync();

        var runtime = frameworkHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var state = await runtime.GetStartedStateForRoutingAsync(CancellationToken.None);
        var discovery = Assert.Single(state.SpotDiscoveries.Values);
        Assert.True(discovery.SpotOwnerSyncEnabled);

        var manager = frameworkHost.Services.GetRequiredService<IZLinkSpotManager>();
        var created = await manager.CreateAsync("registry-stage");
        var spotRoute = discovery.ResolveSpot(created.SpotRid);

        Assert.Equal(runtime.GetSpotNodeRuntime("spot-sync-node").Node.RoutingId,
            spotRoute.OwnerNodeRid);
        Assert.Equal(created.SpotRid, spotRoute.SpotRid);
        Assert.Equal(SpotKind.User, spotRoute.SpotKind);

        await frameworkHost.StopAsync();
        await registryHost.StopAsync();
    }

    private static async Task<T> RetryAsync<T>(
        Func<Task<T>> action,
        Func<T, bool> predicate,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        Exception? lastError = null;
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                var result = await action();
                if (predicate(result))
                {
                    return result;
                }
            }
            catch (Exception ex)
            {
                lastError = ex;
            }

            await Task.Delay(TimeSpan.FromMilliseconds(150));
        }

        if (lastError is not null)
        {
            throw lastError;
        }

        throw new TimeoutException("Discovery integration retry timed out.");
    }
}

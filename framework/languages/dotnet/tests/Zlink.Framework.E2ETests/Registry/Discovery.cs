using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests;

public sealed class DiscoveryTests
{
    [Fact]
    public async Task RegistrySpotRemoteAddresses_Enables_SpotOwnerSync()
    {
        var registryPubEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var registryRouterEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var routeEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var spotNodeEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var spotPubEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
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

            options.UseRegistrySpotRemoteAddresses("registry-spot-sync");
            options.AddRouteMeshChannel("play", route => route.Bind(routeEndpoint));
            options.AddSpotMesh(spotChannel, mesh =>
            {
                mesh.UseDiscovery(discovery => discovery.Add(registryRouterEndpoint));
                mesh.AddNode("spot-sync-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind(spotNodeEndpoint);
                });
                spot.EnablePubSub(pubsub =>
                {
                    pubsub.SetPubBind(spotPubEndpoint);
                });
                spot.AddSpotFactory<SpotTestSupport.LocalSubscriberStageSpot>();
            });
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
        var created = await manager.CreateAsync<SpotTestSupport.LocalSubscriberStageSpot>();
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

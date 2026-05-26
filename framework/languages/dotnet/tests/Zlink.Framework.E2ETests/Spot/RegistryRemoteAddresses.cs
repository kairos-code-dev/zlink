using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.E2ETests;


public sealed class RegistryRemoteAddressesTests : SpotTestSupport
{
    [Fact]
    public async Task RegistrySpotRemoteAddresses_Resolves_Created_Spot_By_Rid_And_Removes_Route()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();
        var spotPubEndpoint = GetFreeTcpEndpoint();
        var routeChannelEndpoint = GetFreeTcpEndpoint();
        var spotChannel = $"game.registry-route.{Guid.NewGuid():N}";

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var frameworkBuilder = Host.CreateApplicationBuilder();
        frameworkBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddRouteMeshChannel("play", routed =>
            {
                routed.Bind(routeChannelEndpoint);
            });
            options.UseRegistrySpotRemoteAddresses("registry-route");
            options.AddSpotMesh(spotChannel, mesh =>
            {
                mesh.UseDiscovery(discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });
                mesh.AddNode("registry-route-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind(spotNodeEndpoint);
                });
                spot.EnablePubSub(pubsub =>
                {
                    pubsub.SetPubBind(spotPubEndpoint);
                });
                spot.AddSpotFactory<LocalSubscriberStageSpot>();
            });
            });
        });

        using var registryHost = registryBuilder.Build();
        using var frameworkHost = frameworkBuilder.Build();

        await registryHost.StartAsync();
        await frameworkHost.StartAsync();

        var manager = frameworkHost.Services.GetRequiredService<IZLinkSpotManager>();
        var resolver = frameworkHost.Services.GetRequiredService<IZLinkSpotRemoteAddressResolver>();

        var created = await manager.CreateAsync<LocalSubscriberStageSpot>();
        var route = await RetryAsync(
            () => resolver.ResolveSpotRemoteAddressAsync(
                created.SpotRid,
                CancellationToken.None).AsTask(),
            static result => result.SpotRid.Size > 0,
            TimeSpan.FromSeconds(5));

        Assert.Equal("play", route.RouterChannelId);
        Assert.Equal(created.SpotRid, route.SpotRid);
        Assert.Equal(ZLinkSpotKind.User, route.SpotKind);

        Assert.True(await manager.RemoveAsync(created.SpotRid));
        var error = await Assert.ThrowsAsync<ZLinkFrameworkException>(() =>
            resolver.ResolveSpotRemoteAddressAsync(created.SpotRid, CancellationToken.None)
                .AsTask());
        Assert.Equal(ZLinkFrameworkErrorKind.SpotRouteNotFound, error.Kind);

        await frameworkHost.StopAsync();
        await registryHost.StopAsync();
    }

    [Fact]
    public async Task RegistrySpotRemoteAddresses_Resolves_Created_Spot_By_Rid()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();
        var spotPubEndpoint = GetFreeTcpEndpoint();
        var routeChannelEndpoint = GetFreeTcpEndpoint();
        var spotChannel = $"game.registry-route-rid.{Guid.NewGuid():N}";

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var frameworkBuilder = Host.CreateApplicationBuilder();
        frameworkBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddRouteMeshChannel("play", routed =>
            {
                routed.Bind(routeChannelEndpoint);
            });
            options.UseRegistrySpotRemoteAddresses("registry-route-rid");
            options.AddSpotMesh(spotChannel, mesh =>
            {
                mesh.UseDiscovery(discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });
                mesh.AddNode("registry-route-rid-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind(spotNodeEndpoint);
                });
                spot.EnablePubSub(pubsub =>
                {
                    pubsub.SetPubBind(spotPubEndpoint);
                });
                spot.AddSpotFactory<LocalSubscriberStageSpot>();
            });
            });
        });

        using var registryHost = registryBuilder.Build();
        using var frameworkHost = frameworkBuilder.Build();

        await registryHost.StartAsync();
        await frameworkHost.StartAsync();

        var manager = frameworkHost.Services.GetRequiredService<IZLinkSpotManager>();
        var resolver = frameworkHost.Services.GetRequiredService<IZLinkSpotRemoteAddressResolver>();

        var created = await manager.CreateAsync<LocalSubscriberStageSpot>();
        var route = await RetryAsync(
            () => resolver.ResolveSpotRemoteAddressAsync(created.SpotRid, CancellationToken.None).AsTask(),
            static result => result.SpotRid.ToString().Length > 0,
            TimeSpan.FromSeconds(5));

        Assert.Equal("play", route.RouterChannelId);
        Assert.Equal(created.SpotRid, route.SpotRid);

        Assert.True(await manager.RemoveAsync(created.SpotRid));

        await frameworkHost.StopAsync();
        await registryHost.StopAsync();
    }

}

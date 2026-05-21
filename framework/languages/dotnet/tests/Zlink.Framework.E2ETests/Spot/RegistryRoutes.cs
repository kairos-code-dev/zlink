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


public sealed class RegistryRoutesTests : SpotTestSupport
{
    [Fact]
    public async Task RegistrySpotRoutes_Resolves_Created_Spot_By_Name()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();
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
            options.UseSpotDiscovery(spotChannel, discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });
            options.AddRouteMeshChannel("play", routed =>
            {
                routed.Bind(routeChannelEndpoint);
            });
            options.UseRegistrySpotRoutes("registry-route");
            options.AddSpotNode("registry-route-node", spot =>
            {
                spot.Bind(spotNodeEndpoint);
                spot.AddSpotFactory<LocalSubscriberStageSpot>("registry-stage");
            });
        });

        using var registryHost = registryBuilder.Build();
        using var frameworkHost = frameworkBuilder.Build();

        await registryHost.StartAsync();
        await frameworkHost.StartAsync();

        var manager = frameworkHost.Services.GetRequiredService<IZLinkSpotManager>();
        var resolver = frameworkHost.Services.GetRequiredService<IZLinkSpotRouteResolver>();

        var created = await manager.CreateAsync("registry-stage");
        var route = await RetryAsync(
            () => resolver.ResolveSpotRouteAsync(
                "registry-stage",
                CancellationToken.None).AsTask(),
            static result => result.SpotRid.Size > 0,
            TimeSpan.FromSeconds(5));

        Assert.Equal("play", route.RouterChannelId);
        Assert.Equal(created.SpotRid, route.SpotRid);
        Assert.Equal(ZLinkSpotKind.User, route.SpotKind);

        Assert.True(await manager.RemoveAsync(created.SpotRid));
        var error = await Assert.ThrowsAsync<ZLinkFrameworkException>(() =>
            resolver.ResolveSpotRouteAsync("registry-stage", CancellationToken.None)
                .AsTask());
        Assert.Equal(ZLinkFrameworkErrorKind.SpotRouteNotFound, error.Kind);

        await frameworkHost.StopAsync();
        await registryHost.StopAsync();
    }

    [Fact]
    public async Task RegistrySpotRoutes_Resolves_Created_Spot_By_Rid()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();
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
            options.UseSpotDiscovery(spotChannel, discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });
            options.AddRouteMeshChannel("play", routed =>
            {
                routed.Bind(routeChannelEndpoint);
            });
            options.UseRegistrySpotRoutes("registry-route-rid");
            options.AddSpotNode("registry-route-rid-node", spot =>
            {
                spot.Bind(spotNodeEndpoint);
                spot.AddSpotFactory<LocalSubscriberStageSpot>("registry-stage-rid");
            });
        });

        using var registryHost = registryBuilder.Build();
        using var frameworkHost = frameworkBuilder.Build();

        await registryHost.StartAsync();
        await frameworkHost.StartAsync();

        var manager = frameworkHost.Services.GetRequiredService<IZLinkSpotManager>();
        var resolver = frameworkHost.Services.GetRequiredService<IZLinkSpotRouteResolver>();

        var created = await manager.CreateAsync("registry-stage-rid");
        var route = await RetryAsync(
            () => resolver.ResolveSpotRouteAsync(created.SpotRid, CancellationToken.None).AsTask(),
            static result => result.SpotRid.ToString().Length > 0,
            TimeSpan.FromSeconds(5));

        Assert.Equal("play", route.RouterChannelId);
        Assert.Equal(created.SpotRid, route.SpotRid);

        Assert.True(await manager.RemoveAsync(created.SpotRid));

        await frameworkHost.StopAsync();
        await registryHost.StopAsync();
    }

}

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


public sealed class EntryRoutingTests : SpotTestSupport
{
    [Fact]
    public async Task EntrySpotRoutingId_IsApplied_ToNativeEntrySpot()
    {
        var spotNodeEndpoint = GetFreeTcpEndpoint();
        var entryRid = RoutingId.FromUtf8("entry");

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh("entry-rid-node", mesh =>
            {
                mesh.UseDiscovery(_ => { });
                mesh.AddNode("entry-rid-node", spot =>
            {
                spot.Bind(spotNodeEndpoint);
                spot.ConfigureEntrySpot(entry =>
                {
                    entry.RoutingId = entryRid;
                });
                spot.AddEntrySpot<GeneralEntrySpot>();
            });
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var activation = runtime
            .GetSpotNodeRuntime("entry-rid-node")
            .EntrySpotActivation
            ?? throw new InvalidOperationException("Entry Spot activation is required.");

        Assert.Equal(entryRid, activation.SpotRid);

        await host.StopAsync();
    }

    [Fact]
    public void AcceptSpotRoutesFromChannel_DiscoveryConnections_AreApplied()
    {
        var channelEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();
        var spotRouterEndpoint = GetFreeTcpEndpoint();

        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery =>
            {
                discovery.Add("tcp://127.0.0.1:5551");
            });

            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind(channelEndpoint);
                    server.ConfigureRouting(routing =>
                    {
                        routing.RoutingId = RoutingId.FromString("aabbcc03");
                    });
                });
            });
            options.AddSpotMesh("spot.route.discovery", mesh =>
            {
                mesh.UseDiscovery(_ => { });
                mesh.AddNode("route-target-node", spot =>
            {
                spot.Bind(spotNodeEndpoint);
                spot.EnableRouter(router => router.Bind(spotRouterEndpoint));
                spot.AcceptSpotRoutesFromChannel("api");
            });
            });
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var node = registration.SpotNodes["route-target-node"];
        var acceptance = Assert.Single(node.AcceptedSpotRouteChannels.Values);

        Assert.Equal("api", acceptance.ChannelName);
        Assert.Empty(acceptance.ManualConnections);
        Assert.Contains("tcp://127.0.0.1:5551", registration.Discovery?.Endpoints ?? []);
    }
}

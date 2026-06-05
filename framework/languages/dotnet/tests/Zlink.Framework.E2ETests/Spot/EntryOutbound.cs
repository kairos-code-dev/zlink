using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests.Spot;

public sealed class EntryOutboundTests : SpotTestSupport
{
    [Fact]
    public async Task EntrySpot_RequestToChannel_Uses_DiscoveredAttachedChannelClient()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var ordersServer = GetFreeTcpEndpoint();
        var spotNode = GetFreeTcpEndpoint();
        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotCallbackRecorder>();
        builder.Services.AddScoped<GeneralEntrySpot>();
        builder.Services.AddScoped<EntrySpotChannelRequestHandler>();
        builder.Services.AddScoped<EntrySpotOrdersRequestHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.AddRegistryEndpoint(registryRouterEndpoint));

            options.AddClientServerChannel("orders", channel =>
            {
                channel.EnableServer(server => server.Bind(ordersServer));
                channel.AddRequestHandler<EntrySpotOrdersRequestHandler, EntrySpotOrderRequest, EntrySpotOrderReply>();
            });
            options.AddSpotMesh("entry.test", mesh =>
            {
                mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint(registryRouterEndpoint));
                mesh.AddNode("entry-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.BindRouter(spotNode);
                });
                spot.AttachChannelClient("orders");
                spot.AddEntrySpot<GeneralEntrySpot>();
            });
            });
        });

        var registryHost = registryBuilder.Build();
        var host = builder.Build();
        await registryHost.StartAsync();
        await host.StartAsync();
        try
        {
            var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var recorder = host.Services.GetRequiredService<EntrySpotCallbackRecorder>();

            await InvokeEntrySpotPacketAsync(
                runtime,
                "entry-node",
                "game.entry-general",
                new EntrySpotChannelRequestCommand("alpha"));

            Assert.Contains("channel-reply:order:alpha", recorder.Events);
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
            await StopAndDisposeHostAsync(registryHost);
        }
    }
}

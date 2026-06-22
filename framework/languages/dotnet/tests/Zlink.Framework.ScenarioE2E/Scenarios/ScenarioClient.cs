using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.ScenarioE2E;

// Client host bootstrap, mirroring the samples' CreateClient / HostFactory: it
// only wires the framework and starts the host. The scenarios resolve the public
// channel/route client from it and call the contract methods directly — the
// interesting messaging stays in the scenario, not behind a helper.
internal static class ScenarioClient
{
    public static async Task<IHost> ConnectChannelAsync(ScenarioContext ctx)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Logging.ClearProviders();
        builder.Services.AddZLinkFramework(framework =>
        {
            var channel = framework.AddClientServerChannel(ctx.Channel);
            foreach (var endpoint in ctx.ZLinkEndpoints)
            {
                channel.EnableClient(endpoint);
            }
        });

        var host = builder.Build();
        await host.StartAsync();
        return host;
    }

    public static async Task<IHost> ConnectRouteMeshAsync(ScenarioContext ctx)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Logging.ClearProviders();
        builder.Services.AddZLinkFramework(framework =>
        {
            var routed = framework.AddRouteMeshChannel(ctx.Channel);
            routed.EnableServer(ctx.ClientZLinkEndpoint);
            routed.SetRoutingId(RoutingId.From(ctx.ClientRoutingId));
            foreach (var peer in ctx.RoutePeerEndpoints)
            {
                routed.EnableClient(peer);
            }
        });

        var host = builder.Build();
        await host.StartAsync();
        return host;
    }
}

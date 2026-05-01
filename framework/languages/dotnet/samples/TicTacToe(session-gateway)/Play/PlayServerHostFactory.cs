using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using TicTacToe.SessionGateway.Configuration;
using Zlink.Framework.AspNetCore;

namespace TicTacToe.SessionGateway.Play;

internal static class PlayServerHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<TicTacToeGameService>();
        builder.Services.AddSingleton<PlayerSessionDirectory>();
        builder.Services.AddScoped<PlaySessionProxy>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
            options.AddRoutedChannel(SampleNames.RouterChannel, routed =>
            {
                routed.Bind(topology.PlayRouterEndpoint);
                routed.ConfigureRouting(routing => routing.RoutingId = topology.PlayRid);
                routed.AddSessionProxyHandler<PlaySessionProxy>();
            });
        });

        return builder.Build();
    }
}

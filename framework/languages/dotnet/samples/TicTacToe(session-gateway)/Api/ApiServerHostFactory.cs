using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using TicTacToe.SessionGateway.Configuration;
using Zlink.Framework.AspNetCore;

namespace TicTacToe.SessionGateway.Api;

internal static class ApiServerHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(new GameLocationStore(topology.PlayRid));
        builder.Services.AddScoped<ApiSessionProxy>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
            options.AddRoutedChannel(SampleNames.RouterChannel, routed =>
            {
                routed.Bind(topology.ApiRouterEndpoint);
                routed.ConfigureRouting(routing => routing.RoutingId = topology.ApiRid);
                routed.AddSessionProxyHandler<ApiSessionProxy>();
            });
        });

        return builder.Build();
    }
}

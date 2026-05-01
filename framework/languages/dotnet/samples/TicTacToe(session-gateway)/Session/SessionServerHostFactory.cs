using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using TicTacToe.SessionGateway.Configuration;
using Zlink.Framework.AspNetCore;

namespace TicTacToe.SessionGateway.Session;

internal static class SessionServerHostFactory
{
    public static IHost Build(
        SampleTopology topology,
        SampleSessionNode sessionNode)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(new SessionRelaySettings(topology.ApiRid));
        builder.Services.AddScoped<SessionRelaySession>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
            options.AddRoutedChannel(SampleNames.RouterChannel, routed =>
            {
                routed.Bind(sessionNode.RouterEndpoint);
                routed.ConfigureRouting(routing => routing.RoutingId = sessionNode.RoutingId);
                routed.EnableSessionGateway();
            });
            options.AddStreamNode(SampleNames.StreamNode, stream =>
            {
                stream.Bind(sessionNode.StreamEndpoint);
                stream.AddHeaderSession<SessionRelaySession>();
            });
        });

        return builder.Build();
    }
}

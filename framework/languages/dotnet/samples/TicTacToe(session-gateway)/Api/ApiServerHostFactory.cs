using Microsoft.Extensions.Hosting;
using TicTacToe.SessionActorDispatch.Configuration;
using Zlink.Framework.AspNetCore;

namespace TicTacToe.SessionActorDispatch.Api;

internal static class ApiServerHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkHandlersFromAssemblyContaining<AuthenticateActorHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
            options.AddChannel(SampleNames.ApiChannel, channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind(topology.ApiChannelEndpoint);
                });
            });
            options.AddChannel(SampleNames.PlayChannel, channel =>
            {
                channel.EnableClient();
            });
        });

        return builder.Build();
    }
}

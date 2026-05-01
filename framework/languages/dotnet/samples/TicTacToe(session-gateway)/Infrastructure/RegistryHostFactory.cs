using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using TicTacToe.SessionActorDispatch.Configuration;
using Zlink.Framework.AspNetCore;

namespace TicTacToe.SessionActorDispatch.Infrastructure;

internal static class RegistryHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = topology.RegistryPubEndpoint;
            options.RouterEndpoint = topology.RegistryRouterEndpoint;
        });

        return builder.Build();
    }
}

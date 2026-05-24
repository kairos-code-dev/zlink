using Microsoft.Extensions.Hosting;
using TicTacToe.SessionGateway.Shared.Configuration;
using Zlink.Framework.AspNetCore;

namespace TicTacToe.SessionGateway.Server.Registry;

public static class RegistryHostFactory
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

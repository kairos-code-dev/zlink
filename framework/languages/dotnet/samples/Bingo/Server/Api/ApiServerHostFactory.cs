using Bingo.Shared.Configuration;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Bingo.Server.Api;

public static class ApiServerHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.DefaultTimeout = SampleTimings.RequestTimeout;
            options.AddHandlersFromAssemblyOf(typeof(ApiServerHostFactory));
            options.Codecs.AddProtobuf();
            options.UseDiscovery(discovery => discovery.AddRegistryEndpoint(topology.RegistryRouterEndpoint));
            options.AddClientServerChannel(SampleNames.ApiChannel, channel =>
            {
                channel.EnableServer(server => server.Bind(topology.ApiChannelEndpoint));
                channel.AddHandlerGroup("api");
            });
            options.AddClientServerChannel(SampleNames.PlayChannel, channel =>
            {
                channel.EnableClient();
            });
        });

        return builder.Build();
    }
}

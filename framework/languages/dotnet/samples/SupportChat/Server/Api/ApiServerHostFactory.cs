using SupportChat.Shared.Configuration;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace SupportChat.Server.Api;

public static class ApiServerHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.DefaultTimeout = SampleTimings.RequestTimeout;
            options.AddHandlersFromAssemblyOf(typeof(ApiServerHostFactory));
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.AddRegistryEndpoint(topology.RegistryRouterEndpoint));
            options.AddClientServerChannel(SampleNames.ApiChannel, channel =>
            {
                channel.EnableServer(server => server.Bind(topology.ApiChannelEndpoint));
                channel.AddHandlerGroup("api");
            });
            options.AddClientServerChannel(SampleNames.SupportChannel, channel =>
            {
                channel.EnableClient();
            });
        });

        return builder.Build();
    }
}

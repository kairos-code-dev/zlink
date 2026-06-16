using SupportChat.Server.Configuration;
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
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            {
                var channel = options.AddClientServerChannel(SampleNames.ApiChannel);
                channel.EnableServer(topology.ApiChannelEndpoint);
                channel.AddHandlerGroup("api");

            }
            {
                var channel = options.AddClientServerChannel(SampleNames.SupportChannel);
                channel.EnableClient();

            }
        });

        return builder.Build();
    }
}

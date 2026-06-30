using Microsoft.Extensions.Hosting;
using SupportChat.Server.Configuration;
using Zlink.Framework.AspNetCore;
using Zlink.Samples.Logging;

namespace SupportChat.Server.Registry;

public static class RegistryHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("SUPPORTCHAT_LOG_DIR"),
            "registry");
        builder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = topology.RegistryPubEndpoint;
            options.RouterEndpoint = topology.RegistryRouterEndpoint;
        });

        return builder.Build();
    }
}

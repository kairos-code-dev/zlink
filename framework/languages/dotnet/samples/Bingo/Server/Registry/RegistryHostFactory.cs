using Bingo.Server.Configuration;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Samples.Logging;

namespace Bingo.Server.Registry;

public static class RegistryHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("BINGO_LOG_DIR"),
            "registry");
        builder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = topology.RegistryPubEndpoint;
            options.RouterEndpoint = topology.RegistryRouterEndpoint;
        });

        return builder.Build();
    }
}

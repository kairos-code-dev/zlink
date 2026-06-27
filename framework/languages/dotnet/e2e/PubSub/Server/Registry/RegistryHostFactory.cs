using PubSub.Server.Registry.Configuration;
using PubSub.Server.Registry;
using Zlink.Framework.AspNetCore;

namespace PubSub.Server.Registry;

internal static class RegistryHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = RegistryOptions.Parse(args);
        var builder = HostFactorySupport.CreateBuilder(args, options.HttpUrl, options.LogDir);
        builder.Services.AddSingleton(options);
        builder.Services.AddSingleton(new EvidenceStore(null));

        builder.Services.AddZLinkRegistry(registry =>
        {
            registry.PubEndpoint = options.RegistryPubEndpoint;
            registry.RouterEndpoint = options.RegistryRouterEndpoint;
        });

        var app = builder.Build();
        app.MapOperationalEndpoints("registry", options.Rid);
        return app;
    }
}

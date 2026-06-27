using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework.AspNetCore;
using RegistryMessaging.Server.Registry.Configuration;
using RegistryMessaging.Server.Registry.Endpoints;
using RegistryMessaging.Server.Registry.Handlers;
using RegistryMessaging.Server.Registry.Infrastructure;

namespace RegistryMessaging.Server.Registry;

internal static class RegistryHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, defaultRole: "registry");
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });

        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore(options.EvidenceFile));

        builder.Services.AddZLinkRegistry(registry =>
        {
            registry.PubEndpoint = options.RegistryPubEndpoint
                ?? throw new InvalidOperationException("--registry-pub-endpoint is required.");
            registry.RouterEndpoint = options.RegistryRouterEndpoint
                ?? throw new InvalidOperationException("--registry-router-endpoint is required.");
        });
        builder.Services.AddZLinkRegistryQueryClient(query =>
        {
            query.Endpoint = options.RegistryRouterEndpoint
                ?? throw new InvalidOperationException("--registry-router-endpoint is required.");
        });

        var app = builder.Build();
        app.MapRegistryMessagingEndpoints(options);
        return app;
    }
}

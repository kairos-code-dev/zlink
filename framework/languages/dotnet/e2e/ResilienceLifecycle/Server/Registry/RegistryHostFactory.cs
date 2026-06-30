using ResilienceLifecycle.Server.Registry.Configuration;
using ResilienceLifecycle.Server.Registry.Endpoints;
using ResilienceLifecycle.Server.Registry.Infrastructure;
using Zlink.Framework.AspNetCore;

namespace ResilienceLifecycle.Server.Registry;

internal static class RegistryHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, "registry");
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
            registry.PubEndpoint = Require(options.RegistryPubEndpoint, "--registry-pub-endpoint");
            registry.RouterEndpoint = Require(options.RegistryRouterEndpoint, "--registry-router-endpoint");
        });
        builder.Services.AddZLinkRegistryQueryClient(query =>
        {
            query.Endpoint = Require(options.RegistryRouterEndpoint, "--registry-router-endpoint");
        });

        var app = builder.Build();
        app.MapRegistryEndpoints(options);
        return app;
    }

    private static string Require(string? value, string name)
    {
        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{name} is required.")
            : value;
    }
}
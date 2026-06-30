using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;
using RegistryMessaging.Server.Consumer.Configuration;
using RegistryMessaging.Server.Consumer.Endpoints;

namespace RegistryMessaging.Server.Consumer;

internal static class ConsumerHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ConsumerOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });

        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.TraceLabel}-flow.log"))
                .TraceLabel(options.TraceLabel);

            var profile = framework.AddClientServerChannel("profile");
            if (!string.IsNullOrWhiteSpace(options.RegistryRouterEndpoint))
            {
                framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint);
                profile.EnableClient();
            }
            else
            {
                foreach (var endpoint in options.ProviderEndpoints)
                {
                    profile.EnableClient(endpoint);
                }
            }

        });

        var app = builder.Build();
        app.MapConsumerEndpoints();
        return app;
    }
}

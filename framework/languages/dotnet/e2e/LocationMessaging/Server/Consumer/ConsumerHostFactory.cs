using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Locations.Redis;
using LocationMessaging.Server.Consumer.Configuration;
using LocationMessaging.Server.Consumer.Endpoints;

namespace LocationMessaging.Server.Consumer;

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
            if (!string.IsNullOrWhiteSpace(options.RedisEndpoint))
            {
                // Endpoint-less client: valid only because the shared Redis
                // location store is registered; the framework auto-connects
                // from live peer rows (doc §2).
                framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint)
                .SetKeyPrefix(options.RedisKeyPrefix!)));
                profile.EnableClient();
            }
            else
            {
                foreach (var endpoint in options.ProviderEndpoints ?? [])
                {
                    profile.EnableClient(endpoint);
                }
            }

            if (string.Equals(options.TraceLabel, "backpressure-consumer", StringComparison.Ordinal))
                profile.ConfigureClientSocket().SendHighWaterMark = 4;

        });

        var app = builder.Build();
        app.MapConsumerEndpoints();
        return app;
    }
}

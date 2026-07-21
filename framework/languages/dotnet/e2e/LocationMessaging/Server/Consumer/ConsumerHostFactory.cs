using Microsoft.Extensions.Configuration;
using Systems.Zlink;

using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Eventing;
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
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });

        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton<ConnectionEvidence>();
        builder.Services.AddScoped<
            IZLinkRuntimeEventHandler<ZLinkMeshRuntimeEvent>,
            MeshConnectionEventObserver>();
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.TraceLabel}-flow.log"))
                .TraceLabel(options.TraceLabel);

            var profileMesh = framework.AddRouteMesh("profile")
                .Listen("tcp://127.0.0.1:0")
                .SetRoutingId(RoutingId.From(options.TraceLabel));
            profileMesh.ChannelName("profile").SetWeight(0);
            if (!string.IsNullOrWhiteSpace(options.RedisEndpoint))
            {
                // Endpoint-less client: valid only because the shared Redis
                // location store is registered; the framework auto-connects
                // from live peer rows (doc §2).
                framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint)
                .SetKeyPrefix(options.RedisKeyPrefix!)));
            }
            else
            {
                foreach (var endpoint in options.ProviderEndpoints ?? [])
                    profileMesh.PeerConnections.Connect(endpoint);
            }

            if (string.Equals(options.TraceLabel, "backpressure-consumer", StringComparison.Ordinal))
                profileMesh.ConfigureRouterSocket().SendHighWaterMark = 4;

        });
        builder.Services.AddZLinkMonitoring(monitor => monitor.AddMeshNodeEvents("profile"));

        var app = builder.Build();
        app.MapConsumerEndpoints();
        return app;
    }
}

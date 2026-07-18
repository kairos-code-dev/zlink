using Microsoft.Extensions.Configuration;

using StoreFailure.Server.Provider.Handlers;
using StoreFailure.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;

using Zlink.Framework.Locations.Redis;

namespace StoreFailure.Server.Provider;

internal static class ProviderHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, "provider");
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
        builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));
        builder.Services.AddSingleton<FaultState>();

        builder.Services.AddZLinkFramework(framework =>
        {
            if (!string.IsNullOrWhiteSpace(options.RedisEndpoint))
            {
                framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                    .SetConnectionString(options.RedisEndpoint)
                    .SetKeyPrefix(options.RedisKeyPrefix
                                  ?? throw new InvalidOperationException("Shared.RedisKeyPrefix is required."))));
                var locations = framework.ConfigureLocations();
                locations.HeartbeatInterval = TimeSpan.FromMilliseconds(options.LocationHeartbeatMs);
                locations.OwnerLeaseTtl = TimeSpan.FromMilliseconds(options.LocationLeaseTtlMs);
                locations.PollingInterval = TimeSpan.FromMilliseconds(options.LocationPollingMs);
                locations.StoreFailureGrace = TimeSpan.FromMilliseconds(options.LocationGraceMs);
            }
            framework.ConfigureDispatch()
                .SetMessageFlowObserver<EvidenceDispatchErrorObserver>()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            var mesh = framework.AddRouteMesh(StoreFailureNames.Channel)
                .Listen(Require(options.ChannelEndpoint, "ChannelEndpoint"))
                .SetRoutingId(RoutingId.From(options.Rid));
            mesh.ChannelName(StoreFailureNames.Channel)
                .SetWeight(options.Weight)
                .AddRequestHandler<ProfileRequestHandler, ProfileReq, ProfileRes>("ProfileReq")
                .AddSendHandler<ProfileCommandHandler, ProfileMsg>("ProfileMsg");
        });

        var app = builder.Build();
        app.MapProviderEndpoints(options);
        return app;
    }

    private static string Require(string? value, string name)
    {
        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{name} is required.")
            : value;
    }
}

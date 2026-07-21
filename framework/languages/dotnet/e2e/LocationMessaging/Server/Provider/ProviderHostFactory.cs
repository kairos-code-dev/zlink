using Microsoft.Extensions.Configuration;

using LocationMessaging.Server.Provider.Configuration;
using LocationMessaging.Server.Provider.Endpoints;
using LocationMessaging.Server.Provider.Handlers;
using LocationMessaging.Server.Provider.Infrastructure;
using LocationMessaging.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Locations.Redis;

namespace LocationMessaging.Server.Provider;

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
        builder.Services.AddScoped<
            IZLinkRuntimeEventHandler<ZLinkMeshRuntimeEvent>,
            ProfileMeshEventObserver>();

        builder.Services.AddZLinkFramework(framework =>
        {
            // The official Redis extension registers the peer/spot/actor/route
            // stores and the owner lease store together (doc §2).
            framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint
                                         ?? throw new InvalidOperationException(
                                             "Shared.RedisEndpoint is required."))
                .SetKeyPrefix(options.RedisKeyPrefix
                                  ?? throw new InvalidOperationException(
                                      "Shared.RedisKeyPrefix is required."))));
            framework.ConfigureDispatch()
                .SetMessageFlowObserver<EvidenceDispatchErrorObserver>()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);

            if (!string.IsNullOrWhiteSpace(options.ChannelEndpoint))
            {
                var profileMesh = framework.AddRouteMesh("profile")
                    .Listen(options.ChannelEndpoint)
                    .SetRoutingId(RoutingId.From(options.Rid));
                var profile = profileMesh.ChannelName("profile").SetWeight(options.Weight);
                var serverSocket = profileMesh.ConfigureRouterSocket();
                serverSocket.ReceiveHighWaterMark = 4;
                if (options.MaxMessageSize > 0) serverSocket.MaxMessageSize = options.MaxMessageSize;
                profile.AddRequestHandler<ProfileRequestHandler, ProfileReq, ProfileRes>("ProfileReq");
                profile.AddRequestHandler<PayloadRequestHandler, PayloadReq, PayloadRes>("PayloadReq");
                profile.AddSendHandler<ProfileCommandHandler, ProfileMsg>("ProfileMsg");
            }

            if (!string.IsNullOrWhiteSpace(options.RouteEndpoint))
            {
                var route = framework.AddRouteMesh("profile.route")
                    .Listen(options.RouteEndpoint)
                    .SetRoutingId(RoutingId.From(options.Rid));
                route.ChannelName("profile.route");
                foreach (var peer in options.RoutePeers ?? []) route.PeerConnections.Connect(peer);

                route.AddRouteRequestHandler<RoutePingHandler, ScenarioRoutePing, ScenarioRoutePong>("ScenarioRoutePing");
            }
        });
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            if (!string.IsNullOrWhiteSpace(options.ChannelEndpoint))
                monitor.AddMeshNodeEvents("profile");
        });

        var app = builder.Build();
        app.MapProviderEndpoints(options);
        return app;
    }
}

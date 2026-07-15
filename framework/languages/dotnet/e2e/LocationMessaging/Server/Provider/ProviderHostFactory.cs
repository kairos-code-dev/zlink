using Microsoft.Extensions.Configuration;

using LocationMessaging.Server.Provider.Configuration;
using LocationMessaging.Server.Provider.Endpoints;
using LocationMessaging.Server.Provider.Handlers;
using LocationMessaging.Server.Provider.Infrastructure;
using LocationMessaging.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
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
        builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>, ProfileSocketEventObserver>();

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
                var clientServer = framework.AddClientServerChannel("profile")
                    .EnableServer(options.ChannelEndpoint)
                    .EnableClient()
                    .SetRoutingId(RoutingId.From(options.Rid));
                var serverSocket = clientServer.ConfigureServerSocket();
                serverSocket.Weight = options.Weight;
                serverSocket.ReceiveHighWaterMark = 4;
                if (options.MaxMessageSize > 0) serverSocket.MaxMessageSize = options.MaxMessageSize;
                clientServer.AddRequestHandler<ProfileRequestHandler, ProfileReq, ProfileRes>("ProfileReq");
                clientServer.AddRequestHandler<PayloadRequestHandler, PayloadReq, PayloadRes>("PayloadReq");
                clientServer.AddSendHandler<ProfileCommandHandler, ProfileMsg>("ProfileMsg");
            }

            if (!string.IsNullOrWhiteSpace(options.ManualClientEndpoint))
                framework.AddClientServerChannel("profile.manual")
                    .EnableClient(options.ManualClientEndpoint);

            if (!string.IsNullOrWhiteSpace(options.RouteEndpoint))
            {
                var route = framework.AddRouteMeshChannel("profile.route")
                    .EnableServer(options.RouteEndpoint)
                    .SetRoutingId(RoutingId.From(options.Rid));
                foreach (var peer in options.RoutePeers ?? []) route.EnableClient(peer);

                route.AddRequestHandler<RoutePingHandler, ScenarioRoutePing, ScenarioRoutePong>("ScenarioRoutePing");
            }
        });
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddSocketEvents("profile.server", ZLinkSocketEventKind.ConnectionReady);
            monitor.AddSocketEvents(
                "profile.client",
                ZLinkSocketEventKind.ConnectionReady,
                ZLinkSocketEventKind.Disconnected);
        });

        var app = builder.Build();
        app.MapProviderEndpoints(options);
        return app;
    }
}

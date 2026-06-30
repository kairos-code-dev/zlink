using RegistryMessaging.Server.Provider.Configuration;
using RegistryMessaging.Server.Provider.Endpoints;
using RegistryMessaging.Server.Provider.Handlers;
using RegistryMessaging.Server.Provider.Infrastructure;
using RegistryMessaging.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;

namespace RegistryMessaging.Server.Provider;

internal static class ProviderHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, "provider");
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

        builder.Services.AddZLinkFramework(framework =>
        {
            framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint
                                                         ?? throw new InvalidOperationException(
                                                             "--registry-router-endpoint is required."));
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
                clientServer.ConfigureServerSocket().Weight = options.Weight;
                clientServer.AddRequestHandler<ProfileRequestHandler, ProfileReq, ProfileRes>("ProfileReq");
                clientServer.AddRequestHandler<PayloadRequestHandler, PayloadReq, PayloadRes>("PayloadReq");
                clientServer.AddSendHandler<ProfileCommandHandler, ProfileMsg>("ProfileMsg");
            }

            if (!string.IsNullOrWhiteSpace(options.ManualClientEndpoint))
                framework.AddClientServerChannel("profile.manual")
                    .EnableClient(options.ManualClientEndpoint);

            if (!string.IsNullOrWhiteSpace(options.RouteEndpoint))
            {
                var route = framework.AddRouteMesh("profile.route")
                    .EnableServer(options.RouteEndpoint)
                    .SetRoutingId(RoutingId.From(options.Rid));
                foreach (var peer in options.RoutePeers) route.EnableClient(peer);

                route.AddRequestHandler<RoutePingHandler, ScenarioRoutePing, ScenarioRoutePong>("ScenarioRoutePing");
            }
        });

        var app = builder.Build();
        app.MapProviderEndpoints(options);
        return app;
    }
}
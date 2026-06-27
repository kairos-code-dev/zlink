using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using RegistryMessaging.Shared;
using Systems.Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;
using RegistryMessaging.Server.Provider.Configuration;
using RegistryMessaging.Server.Provider.Endpoints;
using RegistryMessaging.Server.Provider.Handlers;
using RegistryMessaging.Server.Provider.Infrastructure;

namespace RegistryMessaging.Server.Provider;

internal static class ProviderHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, defaultRole: "provider");
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
                ?? throw new InvalidOperationException("--registry-router-endpoint is required."));
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
                clientServer.AddRequestHandler<ProfileRequestHandler, ProfileRequest, ProfileReply>("ProfileRequest");
                clientServer.AddRequestHandler<PayloadRequestHandler, PayloadRequest, PayloadReply>("PayloadRequest");
                clientServer.AddSendHandler<ProfileCommandHandler, ProfileCommand>("ProfileCommand");
            }

            if (!string.IsNullOrWhiteSpace(options.ManualClientEndpoint))
            {
                framework.AddClientServerChannel("profile.manual")
                    .EnableClient(options.ManualClientEndpoint);
            }

            if (!string.IsNullOrWhiteSpace(options.RouteEndpoint))
            {
                var route = framework.AddRouteMesh("profile.route")
                    .EnableServer(options.RouteEndpoint)
                    .SetRoutingId(RoutingId.From(options.Rid));
                foreach (var peer in options.RoutePeers)
                {
                    route.EnableClient(peer);
                }

                route.AddRequestHandler<RoutePingHandler, ScenarioRoutePing, ScenarioRoutePong>("ScenarioRoutePing");
            }
        });

        var app = builder.Build();
        app.MapProviderEndpoints(options);
        return app;
    }
}

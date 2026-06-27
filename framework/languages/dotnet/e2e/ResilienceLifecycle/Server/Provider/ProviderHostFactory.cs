using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using ResilienceLifecycle.Shared;
using ResilienceLifecycle.Server.Provider.Handlers;
using ResilienceLifecycle.Server.Provider.Support;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;

namespace ResilienceLifecycle.Server.Provider;

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
        builder.Services.AddSingleton<FaultState>();

        builder.Services.AddZLinkFramework(framework =>
        {
            framework.UseDiscovery().AddRegistryEndpoint(Require(options.RegistryRouterEndpoint, "--registry-router-endpoint"));
            framework.ConfigureDispatch()
                .SetMessageFlowObserver<EvidenceDispatchErrorObserver>()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            var channel = framework.AddClientServerChannel(ResilienceLifecycleNames.Channel)
                .EnableServer(Require(options.ChannelEndpoint, "--channel-endpoint"))
                .SetRoutingId(RoutingId.From(options.Rid));
            channel.ConfigureServerSocket().Weight = options.Weight;
            channel.AddRequestHandler<ProfileRequestHandler, ProfileRequest, ProfileReply>("ProfileRequest");
            channel.AddSendHandler<ProfileCommandHandler, ProfileCommand>("ProfileCommand");
        });

        var app = builder.Build();
        app.MapProviderEndpoints(options);
        return app;
    }

    static string Require(string? value, string name)
    {
        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{name} is required.")
            : value;
    }
}

using Microsoft.Extensions.Configuration;

using Systems.Zlink;
using TicTacToe.Server.Api.Handlers;
using TicTacToe.Server.Configuration;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace TicTacToe.Server.Api;

internal sealed class ApiServer(SampleSettings settings)
{
    public WebApplication Build()
    {
        var builder = WebApplication.CreateBuilder();
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        SampleLogging.Configure(builder.Logging, settings.LogDirectory, "api");
        builder.WebHost.UseUrls(settings.ApiBindUrl);
        builder.Services.AddSingleton(settings);
        builder.Services.AddZLinkFramework(options =>
        {
            options.DisableImplicitHandlerAutoRegistration();
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(settings.LogDirectory, settings.InstanceName))
                .TraceLabel(settings.InstanceName);
            options.AddRouteMesh(SampleChannels.Api)
                .Listen(settings.ApiChannelEndpoint)
                .SetRoutingId(RoutingId.From($"{settings.InstanceName}-api"))
                .ChannelName(SampleChannels.Api)
                .AddRequestHandler<AuthenticatePlayerHandler, AuthenticatePlayerReq, AuthenticatePlayerRes>();

            var play0 = options.AddRouteMesh(SampleChannels.Play(0))
                .Listen("tcp://127.0.0.1:0")
                .SetRoutingId(RoutingId.From($"{settings.InstanceName}-play-0"));
            play0.ChannelName(SampleChannels.Play(0));
            play0.PeerConnections.Connect(settings.PlayChannelEndpoints[0]);

            var play1 = options.AddRouteMesh(SampleChannels.Play(1))
                .Listen("tcp://127.0.0.1:0")
                .SetRoutingId(RoutingId.From($"{settings.InstanceName}-play-1"));
            play1.ChannelName(SampleChannels.Play(1));
            play1.PeerConnections.Connect(settings.PlayChannelEndpoints[1]);
        });

        var app = builder.Build();
        app.MapPost("/games", CreateGameHttpHandler.HandleAsync);
        return app;
    }
}

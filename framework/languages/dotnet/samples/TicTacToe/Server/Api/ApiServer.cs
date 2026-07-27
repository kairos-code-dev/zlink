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
                .MessageFlow(ZLinkRuntimeMessageFlowMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(settings.LogDirectory, settings.InstanceName))
                .TraceLabel(settings.InstanceName);
            var mesh = options.AddRouteMesh(SampleNodes.Mesh)
                .Listen(settings.MeshEndpoint)
                .SetRoutingIdPrefix("tictactoe-api");
            mesh.ChannelName(SampleChannels.Api)
                .AddRequestHandler<AuthenticatePlayerHandler, AuthenticatePlayerReq, AuthenticatePlayerRes>();
            mesh.ChannelName(SampleChannels.Play).SetWeight(0);
            mesh.ChannelName(SampleTopics.PlayerMilestoneChannel).SetWeight(0);
            foreach (var endpoint in settings.PeerMeshEndpoints)
                mesh.PeerConnections.Connect(endpoint);
        });

        var app = builder.Build();
        app.MapPost("/games", CreateGameHttpHandler.HandleAsync);
        return app;
    }
}

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
            options.AddClientServerChannel(SampleChannels.Api)
                .EnableServer(settings.ApiChannelEndpoint)
                .AddRequestHandler<AuthenticatePlayerHandler, AuthenticatePlayerReq, AuthenticatePlayerRes>();

            options.AddClientServerChannel(SampleChannels.Play(0))
                .EnableClient(settings.PlayChannelEndpoints[0]);

            options.AddClientServerChannel(SampleChannels.Play(1))
                .EnableClient(settings.PlayChannelEndpoints[1]);
        });

        var app = builder.Build();
        app.MapPost("/games", CreateGameHttpHandler.HandleAsync);
        return app;
    }
}

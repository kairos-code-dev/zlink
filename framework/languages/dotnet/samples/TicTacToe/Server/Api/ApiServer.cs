using TicTacToe.Server.Api.Handlers;
using TicTacToe.Server.Configuration;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.AspNetCore;

namespace TicTacToe.Server.Api;

internal sealed class ApiServer(SampleSettings settings)
{
    public WebApplication Build()
    {
        var builder = WebApplication.CreateBuilder();
        SampleLogging.Configure(builder.Logging, settings, "api");
        builder.WebHost.UseUrls(settings.ApiBindUrl);
        builder.Services.AddSingleton(settings);
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch().SetMessageDispatchErrorObserver<TicTacToeDispatchErrorObserver>();
            options.Codecs.AddJson();

            options.AddClientServerChannel(SampleChannels.Api)
                .EnableServer(settings.ApiChannelEndpoint)
                .AddRequestHandler<AuthenticatePlayerHandler>();

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

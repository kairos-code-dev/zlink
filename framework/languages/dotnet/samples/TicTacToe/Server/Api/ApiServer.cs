using TicTacToe.Server.Api.Handlers;
using TicTacToe.Server.Configuration;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.AspNetCore;

namespace TicTacToe.Server.Api;

internal sealed class ApiServer(SampleSettings settings)
{
    public WebApplication Build()
    {
        var builder = WebApplication.CreateBuilder();
        SampleLogging.Configure(builder.Logging, settings, "api");
        builder.WebHost.UseUrls(settings.ApiBindUrl);
        builder.Services.AddZLinkFramework(options =>
        {
            options.DefaultTimeout = SampleTimeouts.Request;
            options.Codecs.AddJson();
            {
                var channel = options.AddClientServerChannel(SampleChannels.Api)
                    .EnableServer(settings.ApiChannelEndpoint);
                channel.AddRequestHandler<AuthenticatePlayerHandler>();

            }

            {
                options.AddClientServerChannel(SampleChannels.Play)
                    .EnableClient(settings.PlayChannelEndpoint);

            }
        });

        var app = builder.Build();
        app.MapPost("/games", CreateGameHttpHandler.HandleAsync);
        return app;
    }
}

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
            options.AddClientServerChannel(SampleChannels.Api, channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind(settings.ApiChannelEndpoint);
                });
                channel.AddRequestHandler<AuthenticatePlayerHandler>();
            });

            options.AddClientServerChannel(SampleChannels.Play, channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections =>
                    {
                        connections.Connect(settings.PlayChannelEndpoint);
                    });
                });
            });
        });

        var app = builder.Build();
        app.MapPost("/games", CreateGameHttpHandler.HandleAsync);
        return app;
    }
}

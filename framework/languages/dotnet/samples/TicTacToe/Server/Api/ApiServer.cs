using Zlink.Framework.AspNetCore;
using TicTacToe.Server.Api.Handlers;

namespace TicTacToe.Server.Api;

internal sealed class ApiServer(SampleSettings settings)
{
    public WebApplication Build()
    {
        var builder = WebApplication.CreateBuilder();
        SampleLogging.Configure(builder.Logging, settings, "api");
        builder.WebHost.UseUrls(settings.ApiBindUrl);
        builder.Services.AddZLinkHandlersFromAssemblyContaining<AuthenticatePlayerHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddChannel(SampleChannels.Api, channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind(settings.ApiChannelEndpoint);
                });
            });

            options.AddChannel(SampleChannels.Play, channel =>
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

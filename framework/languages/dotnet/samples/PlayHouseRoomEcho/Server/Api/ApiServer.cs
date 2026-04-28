using Zlink.Framework.AspNetCore;
using PlayHouseRoomEcho.Server.Api.Handlers;

namespace PlayHouseRoomEcho.Server.Api;

internal sealed class ApiServer(SampleSettings settings)
{
    public WebApplication Build()
    {
        var builder = WebApplication.CreateBuilder();
        SampleLogging.Configure(builder.Logging, settings, "api");
        builder.WebHost.UseUrls(settings.ApiBindUrl);
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddChannel("Api", channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind(settings.ApiChannelEndpoint);
                });
            });

            options.AddChannel("Play", channel =>
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
        app.MapPost("/rooms", CreateRoomHttpHandler.HandleAsync);
        return app;
    }
}

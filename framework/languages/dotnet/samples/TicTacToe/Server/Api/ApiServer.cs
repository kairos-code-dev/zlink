using Systems.Zlink.Codecs.Json;
using Microsoft.Extensions.Logging;
using TicTacToe.Server.Api;
using TicTacToe.Server.Api.Handlers;
using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play;
using TicTacToe.Server.Play.EntrySpot;
using TicTacToe.Server.Play.GameSpots;
using TicTacToe.Server.Play.Sessions;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Zlink.Framework.Runtime.Core;
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
            options.AddHandlersFromAssemblyOf<ApiServer>();
            options.AddClientServerChannel(SampleChannels.Api, channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind(settings.ApiChannelEndpoint);
                });
                channel.AddHandlerGroup("api");
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

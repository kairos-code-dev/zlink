using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using TicTacToe.Server.Play.Actors;
using TicTacToe.Server.Play.Actors.Handlers;
using TicTacToe.Server.Play.Games;
using TicTacToe.Server.Play.Handlers;
using TicTacToe.Server.Play.Sessions;

namespace TicTacToe.Server.Play;

internal sealed class PlayServer(SampleSettings settings)
{
    public IHost Build()
    {
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(builder.Logging, settings, "play");

        builder.Services.AddSingleton(settings);
        builder.Services.AddScoped<CreateGameHandler>();
        builder.Services.AddScoped<PlaySession>();
        builder.Services.AddScoped<PlaySessionAuthenticator>();
        builder.Services.AddScoped<PlayEntrySpot>();
        builder.Services.AddScoped<PlayActorFactory>();
        builder.Services.AddScoped<TicTacToeJoinService>();
        builder.Services.AddScoped<TicTacToeGameJoinHandler>();
        builder.Services.AddScoped<TicTacToeGameTimerHandler>();
        builder.Services.AddScoped<PlayActorJoinGameHandler>();
        builder.Services.AddScoped<PlayActorPlaceMarkHandler>();
        builder.Services.AddZLinkHandlersFromAssemblyContaining<CreateGameHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddActorFactory<PlayActorFactory>(SampleTypes.PlayerActor);

            options.AddClientServerChannel(SampleChannels.Api, channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections =>
                    {
                        connections.Connect(settings.ApiChannelEndpoint);
                    });
                });
            });

            options.AddClientServerChannel(SampleChannels.Play, channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind(settings.PlayChannelEndpoint);
                });
            });

            options.AddStreamNode(SampleNodes.ClientStream, stream =>
            {
                stream.Bind(settings.PlayEndpoint);
                stream.AddHeaderSession<PlaySession>();
            });

            options.AddSpotMesh("game.tictactoe", spotMesh =>
            {
                spotMesh.AddNode(SampleNodes.PlaySpot, spot =>
                {
                    spot.Bind(settings.SpotEndpoint);
                    spot.AddEntrySpot<PlayEntrySpot>();
                    spot.AddSpotFactory<TicTacToeGame>(SampleTypes.GameSpot);
                });
            });
        });

        return builder.Build();
    }
}

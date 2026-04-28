using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using PlayHouseRoomEcho.Server.Actors;
using PlayHouseRoomEcho.Server.Play.Handlers;

namespace PlayHouseRoomEcho.Server.Play;

internal sealed class PlayServer(SampleSettings settings)
{
    public IHost Build()
    {
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(builder.Logging, settings, "play");

        builder.Services.AddSingleton(settings);
        builder.Services.AddScoped<CreateRoomHandler>();
        builder.Services.AddScoped<RoomStreamSession>();
        builder.Services.AddScoped<GameRoomJoinHandler>();
        builder.Services.AddZLinkHandlersFromAssemblyContaining<CreateRoomHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.room", _ => { });
            options.AddActorFactory<PlayerActorFactory>("player");

            options.AddChannel("Play", channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind(settings.PlayChannelEndpoint);
                });
            });

            options.AddStreamNode("client-stream", stream =>
            {
                stream.Bind(settings.PlayEndpoint);
                stream.AddHeaderSession<RoomStreamSession>();
            });

            options.AddSpotNode("play-node", spot =>
            {
                spot.Bind(settings.SpotEndpoint);
                spot.AddSpotFactory<GameRoom>("room");
            });
        });

        return builder.Build();
    }
}

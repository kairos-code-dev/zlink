using Bingo.Shared.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Bingo.Server.Play;

public static class PlayServerHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<BingoRoomDirectory>();
        builder.Services.AddSingleton<BingoNotificationPublisher>();
        builder.Services.AddScoped<PlayerActorFactory>();
        builder.Services.AddScoped<BingoEntrySpot>();
        builder.Services.AddScoped<EnsurePlayerActorHandler>();
        builder.Services.AddScoped<AllocateBingoRoomHandler>();
        builder.Services.AddScoped<MatchBingoActorHandler>();
        builder.Services.AddScoped<BingoRoomJoinHandler>();
        builder.Services.AddScoped<StartBingoGameHandler>();
        builder.Services.AddScoped<BingoRoomTimerHandler>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf(typeof(PlayServerHostFactory));
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
            options.AddClientServerChannel(SampleNames.PlayChannel, channel =>
            {
                channel.EnableServer(server => server.Bind(topology.PlayChannelEndpoint));
                channel.AddHandlerGroup("play");
            });
            options.AddClientServerChannel(SampleNames.ApiChannel, channel =>
            {
                channel.EnableClient();
            });
            options.AddActorFactory<PlayerActorFactory>(SampleNames.PlayerActorType);
            options.UseRegistryActorRoutes("bingo");
            options.UseRegistrySpotRoutes("bingo");
            options.UseRegistryActorSessionBindings("bingo");
            options.AddRouteMeshChannel(SampleNames.RouterChannel, routed =>
            {
                routed.Bind(topology.PlayRouterEndpoint);
                routed.ConfigureRouting(routing => routing.RoutingId = topology.PlayRid);
            });
            options.AddSpotMesh(SampleNames.RoomSpotDiscovery, spotMesh =>
            {
                spotMesh.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
                spotMesh.AddNode(SampleNames.RoomSpotNode, spot =>
                {
                    spot.Bind(topology.PlaySpotEndpoint);
                    spot.EnableRouter();
                    spot.AcceptSpotRoutesFromChannel(SampleNames.RouterChannel);
                    spot.AddEntrySpot<BingoEntrySpot>();
                    spot.AddSpotFactory<BingoRoomSpot>(SampleNames.RoomSpotType);
                });
            });
        });

        return builder.Build();
    }
}

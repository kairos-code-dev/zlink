using Bingo.SessionGateway.Infrastructure;
using Bingo.SessionGateway.Infrastructure.Configuration;
using Bingo.SessionGateway.Shared.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Bingo.SessionGateway.Play;

public static class PlayServerHostFactory
{
    public static IHost Build(
        SampleTopology topology,
        RegistryActorSessionLocationStore sessionLocations,
        RegistryPlayRouteStore playRoutes)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton(sessionLocations);
        builder.Services.AddSingleton(playRoutes);
        builder.Services.AddSingleton<ISpotRouteResolver>(playRoutes);
        builder.Services.AddSingleton<ISpotRouteWriter>(playRoutes);
        builder.Services.AddSingleton<RegistryPlayRoutePublisher>();
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
            });
            options.AddClientServerChannel(SampleNames.ApiChannel, channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections => connections.Connect(topology.ApiChannelEndpoint));
                });
            });
            options.AddActorFactory<PlayerActorFactory>(SampleNames.PlayerActorType);
            options.AddActorPlayRouteResolver<RegistryPlayRouteStore>();
            options.AddActorSessionBindingStore<RegistryActorSessionLocationStore>();
            options.AddSpotRouteResolver<RegistryPlayRouteStore>();
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
                    spot.AddEntrySpot<BingoEntrySpot>();
                    spot.AddSpotFactory<BingoRoomSpot>(SampleNames.RoomSpotType);
                });
            });
        });

        return builder.Build();
    }
}
